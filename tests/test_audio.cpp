#include "audio/audio_output.h"
#include "audio/audio_worker.h"
#include "audio/ring_buffer.h"
#include "media/audio_decoder.h"
#include "media/media_error.h"
#include "test_clip.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <numeric>
#include <thread>
#include <vector>

using namespace sharkcut;
using namespace sharkcut::test;

namespace {

constexpr std::int64_t kSamplesPerFrame = 800; // 48 000 / 60

struct TempClip {
    std::filesystem::path path;
    explicit TempClip(const ClipOptions& options, const std::string& name)
        : path(tempClipPath(name)) {
        writeTestClip(path, options);
    }
    ~TempClip() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};

ClipOptions pcmClip(int delayFrames = 0) {
    ClipOptions o;
    o.frames = 120;
    o.audio = ClipOptions::Audio::Pcm;
    o.container = "matroska";
    o.audioDelayFrames = delayFrames;
    return o;
}

std::vector<float> readAll(AudioDecoder& decoder) {
    std::vector<float> all;
    std::vector<float> chunk(1000 * kAudioChannels);
    while (const std::size_t n = decoder.read(chunk.data(), 1000)) {
        all.insert(all.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(n * 2));
    }
    return all;
}

} // namespace

TEST_CASE("AudioRingBuffer conserva el orden al dar la vuelta", "[audio]") {
    AudioRingBuffer ring(10);
    CHECK(ring.capacity() == 10);
    std::vector<float> data(8);
    std::iota(data.begin(), data.end(), 0.0f);

    CHECK(ring.write(data.data(), 8) == 8);
    std::vector<float> out(6);
    CHECK(ring.read(out.data(), 6) == 6);
    CHECK(out == std::vector<float>{0, 1, 2, 3, 4, 5});

    // Escribir 8 más: solo caben 8 (quedan 2 dentro), y dan la vuelta al final.
    std::iota(data.begin(), data.end(), 100.0f);
    CHECK(ring.write(data.data(), 20) == 8);
    CHECK(ring.available() == 10);
    CHECK(ring.freeSpace() == 0);
    CHECK(ring.write(data.data(), 1) == 0);

    out.assign(12, -1.0f);
    CHECK(ring.read(out.data(), 12) == 10);
    CHECK(out[0] == 6);
    CHECK(out[1] == 7);
    CHECK(out[2] == 100);
    CHECK(out[9] == 107);

    ring.reset();
    CHECK(ring.available() == 0);
}

TEST_CASE("AudioDecoder entrega el audio alineado con cada fotograma", "[audio]") {
    TempClip clip(pcmClip(), "pcm.mkv");
    AudioDecoder decoder(clip.path.string());
    CHECK(decoder.info().codec == "pcm_s16le");
    CHECK(decoder.info().sampleRate == 48'000);
    CHECK(decoder.info().channels == 2);

    const std::vector<float> all = readAll(decoder);
    REQUIRE(all.size() == 120u * kSamplesPerFrame * 2);
    for (int i = 0; i < 120; ++i) {
        CAPTURE(i);
        // Mitad del fotograma, canal izquierdo y derecho.
        const std::size_t at = (static_cast<std::size_t>(i) * kSamplesPerFrame + 400) * 2;
        REQUIRE(all[at] == Catch::Approx(audioLevelFor(i)).margin(1e-4));
        REQUIRE(all[at + 1] == Catch::Approx(audioLevelFor(i)).margin(1e-4));
    }
    CHECK(decoder.read(nullptr, 0) == 0);
}

TEST_CASE("AudioDecoder salta a la muestra exacta", "[audio]") {
    TempClip clip(pcmClip(), "pcm.mkv");
    AudioDecoder decoder(clip.path.string());

    std::vector<float> chunk(256 * kAudioChannels);
    for (const std::int64_t target :
         {std::int64_t{57} * kSamplesPerFrame, std::int64_t{0},
          std::int64_t{119} * kSamplesPerFrame + 399, std::int64_t{30'123},
          std::int64_t{1} * kSamplesPerFrame - 1}) {
        CAPTURE(target);
        decoder.seekToSample(target);
        CHECK(decoder.position() == target);
        REQUIRE(decoder.read(chunk.data(), 1) == 1);
        const int frame = static_cast<int>(target / kSamplesPerFrame);
        REQUIRE(chunk[0] == Catch::Approx(audioLevelFor(frame)).margin(1e-4));
        CHECK(decoder.position() == target + 1);
    }
}

TEST_CASE("AudioDecoder rellena con silencio si el audio empieza tarde", "[audio]") {
    TempClip clip(pcmClip(10), "pcm-tarde.mkv");
    AudioDecoder decoder(clip.path.string());
    const std::vector<float> all = readAll(decoder);

    // Los primeros 10 fotogramas (8000 muestras) son silencio; después, el fotograma 0
    // del audio. El contenedor guarda los tiempos en milisegundos: margen de 1 ms.
    for (std::size_t s = 0; s < 10 * kSamplesPerFrame - 48; ++s) {
        REQUIRE(all[s * 2] == 0.0f);
    }
    const std::size_t at = (10 * kSamplesPerFrame + 400) * 2;
    CHECK(all[at] == Catch::Approx(audioLevelFor(0)).margin(1e-4));
}

TEST_CASE("AudioDecoder lee AAC en MP4 fragmentado como el de OBS", "[audio]") {
    ClipOptions o;
    o.frames = 120;
    o.audio = ClipOptions::Audio::Aac;
    o.fragmented = true;
    TempClip clip(o, "aac.mp4");

    AudioDecoder decoder(clip.path.string());
    CHECK(decoder.info().codec == "aac");
    const std::vector<float> all = readAll(decoder);
    // AAC trabaja en bloques de 1024 muestras: la duración puede variar un bloque.
    const auto samples = static_cast<std::int64_t>(all.size() / 2);
    CHECK(std::abs(samples - 120 * kSamplesPerFrame) <= 2048);

    decoder.seekToSample(48'000);
    std::vector<float> chunk(100 * kAudioChannels);
    CHECK(decoder.read(chunk.data(), 100) == 100);
    CHECK(decoder.position() == 48'100);
}

TEST_CASE("AudioDecoder rechaza archivos sin audio", "[audio]") {
    ClipOptions o;
    o.frames = 30;
    TempClip clip(o, "sin-audio.mp4");
    CHECK_THROWS_AS(AudioDecoder(clip.path.string()), MediaError);
}

TEST_CASE("AudioWorker alimenta la salida y el reloj avanza", "[audio]") {
    // Dispositivo nulo: consume a velocidad real sin sonar (no depende de la tarjeta).
    auto output = std::make_unique<AudioOutput>(AudioOutput::Device::Null);
    CHECK(output->backendName() == "Null");

    TempClip clip(pcmClip(), "pcm.mkv");
    std::atomic<bool> opened = false;
    AudioWorker::Callbacks callbacks;
    callbacks.opened = [&](const AudioStreamInfo&) { opened = true; };
    AudioWorker worker(clip.path.string(), *output, callbacks);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!opened && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(opened);

    const std::uint64_t id = worker.play(24'000); // desde 0,5 s
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    const AudioOutput::Clock clock = output->clock();
    CHECK(clock.requestId == id);
    // Ha sonado algo más allá del punto de partida, pero no más de lo transcurrido.
    CHECK(clock.sample > 24'000);
    CHECK(clock.sample <= 24'000 + 48'000);

    worker.pause();
    const std::int64_t paused = output->clock().sample;
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CHECK(output->clock().sample == paused); // en pausa el reloj no avanza
}
