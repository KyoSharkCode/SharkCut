#include "audio/audio_output.h"
#include "audio/audio_worker.h"
#include "audio/ring_buffer.h"
#include "core/edit.h"
#include "core/sfx.h"
#include "media/audio_decoder.h"
#include "media/media_error.h"
#include "media/media_info.h"
#include "media/sequence_audio.h"
#include "media/timeline_audio.h"
#include "media/waveform.h"
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
    const std::string path = clip.path.string();
    AudioWorker worker([path] { return std::make_unique<AudioDecoder>(path); }, *output, callbacks);

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

    // El medidor de la mezcla recibe el pico de lo que ha sonado (fotogramas 30–72).
    const AudioOutput::Levels levels = output->takePeakLevels();
    CHECK(levels.left > audioLevelFor(30));
    CHECK(levels.left <= audioLevelFor(80));
    CHECK(levels.right == Catch::Approx(levels.left));

    worker.pause();
    const std::int64_t paused = output->clock().sample;
    output->takePeakLevels();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CHECK(output->clock().sample == paused);      // en pausa el reloj no avanza
    CHECK(output->takePeakLevels().left == 0.0f); // ni suena nada
}

TEST_CASE("SequenceAudioSource mezcla A y B con fundido cruzado", "[audio][sequence]") {
    TempClip clipA(pcmClip(), "a.mkv");
    TempClip clipB(pcmClip(), "b.mkv");
    // A: fotogramas 10–69 (60), B: 20–109 (90), transición de 20 → 130 fotogramas.
    const SequenceLayout layout = makeLayout({10, 70}, {20, 110}, 20);
    SequenceAudioSource source(layout, {60, 1}, clipA.path.string(), clipB.path.string());

    std::vector<float> all;
    std::vector<float> chunk(1000 * kAudioChannels);
    while (const std::size_t n = source.read(chunk.data(), 1000)) {
        all.insert(all.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(n * 2));
    }
    REQUIRE(all.size() == 130u * kSamplesPerFrame * 2);

    auto sampleAt = [&](std::int64_t frame) {
        return all[static_cast<std::size_t>(frame * kSamplesPerFrame + 400) * 2];
    };
    // Antes de la transición: solo A (fotograma 10 + t).
    CHECK(sampleAt(5) == Catch::Approx(audioLevelFor(15)).margin(1e-4));
    // Después: solo B (fotograma 20 + t − 40).
    CHECK(sampleAt(100) == Catch::Approx(audioLevelFor(80)).margin(1e-4));
    // En la transición (t = 50, 10 fotogramas dentro de 20): mezcla de potencia constante.
    const double x = (10.0 * kSamplesPerFrame + 400.0 + 0.5) / (20.0 * kSamplesPerFrame);
    const double expected = std::cos(x * 3.14159265358979 / 2) * audioLevelFor(60) +
                            std::sin(x * 3.14159265358979 / 2) * audioLevelFor(30);
    CHECK(sampleAt(50) == Catch::Approx(expected).margin(1e-4));

    // Tras una búsqueda se obtiene lo mismo que leyendo de corrido.
    source.seekToSample(50 * kSamplesPerFrame + 400);
    float stereo[2] = {};
    REQUIRE(source.read(stereo, 1) == 1);
    CHECK(stereo[0] == Catch::Approx(sampleAt(50)).margin(1e-6));
    source.seekToSample(100 * kSamplesPerFrame + 400);
    REQUIRE(source.read(stereo, 1) == 1);
    CHECK(stereo[0] == Catch::Approx(sampleAt(100)).margin(1e-6));
}

TEST_CASE("SequenceAudioSource pone silencio en el clip sin audio", "[audio][sequence]") {
    TempClip clipA(pcmClip(), "a.mkv");
    ClipOptions silent;
    silent.frames = 120;
    TempClip clipB(silent, "b-sin-audio.mp4");
    const SequenceLayout layout = makeLayout({0, 60}, {0, 60}, 10);
    SequenceAudioSource source(layout, {60, 1}, clipA.path.string(), clipB.path.string());

    source.seekToSample(100 * kSamplesPerFrame);
    float stereo[2] = {1.0f, 1.0f};
    REQUIRE(source.read(stereo, 1) == 1);
    CHECK(stereo[0] == 0.0f);

    CHECK_THROWS_AS(SequenceAudioSource(layout, {60, 1}, clipB.path.string(), clipB.path.string()),
                    MediaError);
}

namespace {

Project audioProject(const std::filesystem::path& path) {
    Project p;
    MediaItem item;
    item.path = path.string();
    item.name = "a.mkv";
    item.durationUs = 2'000'000; // 120 fotogramas a 60 fps
    item.hasVideo = true;
    item.hasAudio = true;
    addMedia(p, item);
    return p;
}

std::vector<float> readAll(AudioSource& source) {
    std::vector<float> all;
    std::vector<float> chunk(1000 * kAudioChannels);
    while (const std::size_t n = source.read(chunk.data(), 1000)) {
        all.insert(all.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(n * 2));
    }
    return all;
}

} // namespace

TEST_CASE("TimelineAudioSource plays clips in order with silent gaps", "[audio][timeline]") {
    TempClip clip(pcmClip(), "timeline.mkv");
    Project p = audioProject(clip.path);
    const ObjectId media = p.media[0].id;
    // Entradas en múltiplos de 3 fotogramas (50 ms): el MKV de prueba guarda los tiempos
    // en milisegundos y en otros puntos el audio empezaría unas muestras tarde.
    placeClip(p, media, 0, 12, 30);  // fotogramas 0–30: medio 12–42
    placeClip(p, media, 40, 60, 20); // 40–60: medio 60–80 (hueco de 30 a 40)
    TimelineAudioSource source(p);

    const std::vector<float> all = readAll(source);
    REQUIRE(all.size() == 60u * kSamplesPerFrame * 2);
    auto sampleAt = [&](std::int64_t sample) { return all[static_cast<std::size_t>(sample) * 2]; };

    CHECK(sampleAt(5 * kSamplesPerFrame + 400) == Catch::Approx(audioLevelFor(17)).margin(1e-4));
    CHECK(sampleAt(35 * kSamplesPerFrame + 400) == 0.0f);
    CHECK(sampleAt(50 * kSamplesPerFrame + 400) == Catch::Approx(audioLevelFor(70)).margin(1e-4));

    // Fundido corto al empezar cada clip y al terminar.
    const float first = audioLevelFor(12);
    CHECK(sampleAt(0) == Catch::Approx(first * 0.5f / 96.0f).margin(1e-5));
    CHECK(sampleAt(48) == Catch::Approx(first * 48.5f / 96.0f).margin(1e-5));
    CHECK(sampleAt(30 * kSamplesPerFrame - 1) ==
          Catch::Approx(audioLevelFor(41) * 0.5f / 96.0f).margin(1e-5));

    // Saltar da lo mismo que leer de corrido.
    source.seekToSample(50 * kSamplesPerFrame + 400);
    float stereo[2] = {};
    REQUIRE(source.read(stereo, 1) == 1);
    CHECK(stereo[0] == Catch::Approx(sampleAt(50 * kSamplesPerFrame + 400)).margin(1e-6));
}

TEST_CASE("TimelineAudioSource does not fade inside a split clip", "[audio][timeline]") {
    TempClip clip(pcmClip(), "timeline-split.mkv");
    Project p = audioProject(clip.path);
    placeClip(p, p.media[0].id, 0, 0, 60);
    splitAt(p, 20);
    TimelineAudioSource source(p);
    const std::vector<float> all = readAll(source);
    const auto at = static_cast<std::size_t>(20 * kSamplesPerFrame) * 2;
    CHECK(all[at] == Catch::Approx(audioLevelFor(20)).margin(1e-4));
    CHECK(all[at - 2] == Catch::Approx(audioLevelFor(19)).margin(1e-4));

    Project empty = audioProject(clip.path);
    CHECK_THROWS_AS(TimelineAudioSource(empty), MediaError);
}

TEST_CASE("TimelineAudioSource mixes tracks with clip volume", "[audio][timeline]") {
    TempClip clip(pcmClip(), "timeline-mix.mkv");
    Project p = audioProject(clip.path);
    const ObjectId media = p.media[0].id;
    // A1: medio 0–60; A2: el mismo medio desde 60, a −6 dB.
    const ObjectId first = placeClip(p, media, 0, 0, 60);
    const ObjectId second = placeClip(p, media, 0, 60, 60, {.video = 1, .audio = 1});
    REQUIRE(first != 0);
    REQUIRE(second != 0);
    const ObjectId music = p.clip(second)->link;
    REQUIRE(setClipVolume(p, music, -6.0));
    const auto gain = static_cast<float>(gainFromDb(-6.0));

    {
        TimelineAudioSource source(p);
        const std::vector<float> all = readAll(source);
        const auto at = static_cast<std::size_t>(10 * kSamplesPerFrame + 400) * 2;
        CHECK(all[at] == Catch::Approx(audioLevelFor(10) + gain * audioLevelFor(70)).margin(1e-4));
    }

    // A2 silenciada: solo suena A1.
    REQUIRE(setTrackMuted(p, *p.trackIndex(TrackKind::Audio, 1), true));
    {
        TimelineAudioSource source(p);
        const std::vector<float> all = readAll(source);
        const auto at = static_cast<std::size_t>(10 * kSamplesPerFrame + 400) * 2;
        CHECK(all[at] == Catch::Approx(audioLevelFor(10)).margin(1e-4));
    }

    // Todo silenciado: no hay audio.
    REQUIRE(setTrackMuted(p, *p.trackIndex(TrackKind::Audio, 0), true));
    CHECK_FALSE(p.hasAudibleAudio());
    CHECK_THROWS_AS(TimelineAudioSource(p), MediaError);
}

TEST_CASE("Waveform peaks follow the audio and survive the disk cache", "[audio][waveform]") {
    TempClip clip(pcmClip(), "waveform.mkv");
    const Waveform waveform = computeWaveform(clip.path.string());
    // 120 fotogramas a 60 fps = 2 s = 400 bloques de 5 ms.
    REQUIRE(waveform.peaks.size() == 400);
    // El bloque 10 (muestras 2400–2640) cae dentro del fotograma 3.
    CHECK(waveform.peaks[10] == Waveform::encode(audioLevelFor(3)));
    CHECK(waveform.peaks[399] == Waveform::encode(audioLevelFor(119)));
    CHECK(Waveform::encode(1.0f) == 255);
    CHECK(Waveform::encode(0.0f) == 0);
    CHECK(Waveform::level(255) == 1.0);
    CHECK(Waveform::level(255, -60.0) == 0.0);
    CHECK(Waveform::level(0, 12.0) == 0.0);
    CHECK(Waveform::level(255, -12.0) == Catch::Approx(0.5).margin(0.01));

    const std::filesystem::path file = tempClipPath("ondas") / "prueba.onda";
    REQUIRE(saveWaveform(waveform, file));
    const auto loaded = loadWaveform(file);
    REQUIRE(loaded.has_value());
    CHECK(*loaded == waveform);
    std::error_code ignored;
    std::filesystem::remove_all(file.parent_path(), ignored);

    CHECK_FALSE(waveformCacheName(clip.path.string()).empty());
    CHECK(waveformCacheName("/no/existe.mp4").empty());

    std::atomic<bool> cancel{true};
    CHECK_THROWS_AS(computeWaveform(clip.path.string(), &cancel), MediaError);
}

TEST_CASE("Sound effects in the catalog exist and match their duration", "[audio][sfx]") {
    const std::filesystem::path folder =
        std::filesystem::path(SHARKCUT_SOURCE_DIR) / "presets" / "sfx";
    for (const SoundEffect& effect : kSoundEffects) {
        CAPTURE(effect.file);
        const auto path = folder / std::string(effect.file);
        REQUIRE(std::filesystem::exists(path));
        const MediaInfo info = probeMedia(path.string());
        REQUIRE(info.audio.size() == 1);
        CHECK(std::abs(info.durationUs - effect.durationUs) < 2'000);
        CHECK(effect.hitUs <= effect.durationUs);
    }
    // Cada transición tiene un sonido por defecto que existe (o ninguno).
    for (const TransitionInfo& info : kTransitions) {
        const std::string_view id = defaultSoundFor(info.type);
        CHECK((id.empty() || soundEffect(id) != nullptr));
    }
    CHECK(soundEffect("no-existe") == nullptr);
}
