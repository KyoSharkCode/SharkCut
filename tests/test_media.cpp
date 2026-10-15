#include "media/decode_worker.h"
#include "media/media_error.h"
#include "media/media_info.h"
#include "media/proxy.h"
#include "media/video_decoder.h"
#include "test_clip.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>

using namespace sharkcut;
using namespace sharkcut::test;

namespace {

// Clip temporal que se borra al terminar el test.
struct TempClip {
    std::filesystem::path path;
    std::string codec;

    explicit TempClip(const ClipOptions& options, const std::string& name = "clip.mp4")
        : path(tempClipPath(name)), codec(writeTestClip(path, options)) {}
    ~TempClip() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};

int averageLuma(const VideoFrame& f, int centerX) {
    long sum = 0;
    int count = 0;
    for (int y = f.height / 2 - 4; y < f.height / 2 + 4; ++y) {
        for (int x = centerX - 4; x < centerX + 4; ++x) {
            sum += f.y[static_cast<std::size_t>(y) * f.width + x];
            ++count;
        }
    }
    return static_cast<int>(sum / count);
}

// Índice de fotograma que lleva dibujado el contenido del fotograma.
int contentIndex(const VideoFrame& f) {
    return decodeIndex(averageLuma(f, f.width / 4), averageLuma(f, f.width * 3 / 4));
}

template <typename Predicate> bool waitFor(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

} // namespace

TEST_CASE("probeMedia lee resolucion, fps y numero de fotogramas", "[media]") {
    const bool fragmented = GENERATE(false, true);
    CAPTURE(fragmented);
    TempClip clip({.frames = 120, .fragmented = fragmented});

    const MediaInfo info = probeMedia(clip.path.string());
    REQUIRE(info.video.has_value());
    CHECK(info.video->codec == clip.codec);
    CHECK(info.video->width == 320);
    CHECK(info.video->height == 180);
    CHECK(info.video->frameRate == FrameRate{60, 1});
    CHECK(info.video->frameCount == 120);
    CHECK_FALSE(info.video->variableFrameRate);
    CHECK(info.audio.empty());
    CHECK(info.durationUs >= 1'990'000);
    CHECK(info.durationUs <= 2'010'000);
}

TEST_CASE("Los fps son exactos aunque los paquetes no indiquen su duracion", "[media]") {
    // Sin duraciones, el MP4 calcula unos fps medios de 7200/119 (≈ 60,5).
    TempClip clip({.frames = 120, .packetDurations = false});

    const MediaInfo info = probeMedia(clip.path.string());
    REQUIRE(info.video.has_value());
    CHECK(info.video->frameRate == FrameRate{60, 1});
    CHECK(info.video->frameCount == 120);
    CHECK_FALSE(info.video->variableFrameRate);

    VideoDecoder decoder(clip.path.string(), HwAccel::None);
    decoder.seekToFrame(100);
    const auto frame = decoder.nextFrame();
    REQUIRE(frame.has_value());
    CHECK(frame->index == 100);
    CHECK(contentIndex(*frame) == 100);
}

TEST_CASE("probeMedia rechaza archivos inexistentes", "[media]") {
    CHECK_THROWS_AS(probeMedia("no-existe-sharkcut.mp4"), MediaError);
    CHECK_THROWS_AS(VideoDecoder("no-existe-sharkcut.mp4", HwAccel::None), MediaError);
}

TEST_CASE("VideoDecoder entrega todos los fotogramas en orden", "[media]") {
    const bool fragmented = GENERATE(false, true);
    CAPTURE(fragmented);
    TempClip clip({.frames = 120, .fragmented = fragmented});

    VideoDecoder decoder(clip.path.string(), HwAccel::None);
    CHECK(decoder.hwAccelName().empty());
    for (int i = 0; i < 120; ++i) {
        const auto frame = decoder.nextFrame();
        REQUIRE(frame.has_value());
        REQUIRE(frame->index == i);
        REQUIRE(contentIndex(*frame) == i);
        REQUIRE(frame->width == 320);
        REQUIRE(frame->height == 180);
        REQUIRE(frame->y.size() == 320u * 180u);
        REQUIRE(frame->uv.size() == 160u * 2u * 90u);
        REQUIRE(frame->matrix == ColorMatrix::Bt709);
        REQUIRE_FALSE(frame->fullRange);
    }
    CHECK_FALSE(decoder.nextFrame().has_value());
}

TEST_CASE("VideoDecoder salta al fotograma exacto", "[media]") {
    const bool fragmented = GENERATE(false, true);
    CAPTURE(fragmented);
    TempClip clip({.frames = 120, .gopSize = 30, .fragmented = fragmented});

    VideoDecoder decoder(clip.path.string(), HwAccel::None);
    // Destinos en fotogramas clave, justo antes y después, hacia delante y hacia atrás.
    for (const std::int64_t target : {57, 0, 119, 31, 90, 1, 60, 59, 30, 29, 118, 45}) {
        CAPTURE(target);
        decoder.seekToFrame(target);
        const auto frame = decoder.nextFrame();
        REQUIRE(frame.has_value());
        REQUIRE(frame->index == target);
        REQUIRE(contentIndex(*frame) == target);
        if (target < 119) {
            const auto after = decoder.nextFrame();
            REQUIRE(after.has_value());
            REQUIRE(after->index == target + 1);
            REQUIRE(contentIndex(*after) == target + 1);
        }
    }
}

TEST_CASE("Buscar mas alla del final muestra el ultimo fotograma", "[media]") {
    TempClip clip({.frames = 120});
    VideoDecoder decoder(clip.path.string(), HwAccel::None);
    decoder.seekToFrame(5'000);
    const auto frame = decoder.nextFrame();
    REQUIRE(frame.has_value());
    CHECK(frame->index == 119);
    CHECK(contentIndex(*frame) == 119);
    CHECK_FALSE(decoder.nextFrame().has_value());
}

TEST_CASE("DecodeWorker prepara fotogramas y atiende busquedas", "[media]") {
    TempClip clip({.frames = 120, .fragmented = true});

    std::atomic<bool> opened = false;
    std::atomic<std::int64_t> frameCount = 0;
    DecodeWorker::Callbacks callbacks;
    callbacks.opened = [&](const VideoStreamInfo& info) {
        frameCount = info.frameCount;
        opened = true;
    };
    DecodeWorker worker(clip.path.string(), HwAccel::None, callbacks, 6);

    REQUIRE(waitFor([&] { return opened.load(); }));
    CHECK(frameCount == 120);

    // Al abrir, prepara los primeros fotogramas.
    REQUIRE(waitFor([&] { return worker.hasFrame(5); }));
    auto frame = worker.takeUpTo(3);
    REQUIRE(frame);
    CHECK(frame->index == 3);
    CHECK(contentIndex(*frame) == 3);

    // Una búsqueda vacía la cola y continúa desde el destino.
    worker.seek(77);
    REQUIRE(waitFor([&] { return worker.hasFrame(77); }));
    frame = worker.takeUpTo(77);
    REQUIRE(frame);
    CHECK(frame->index == 77);
    CHECK(contentIndex(*frame) == 77);

    // Reproducir hasta el final.
    std::int64_t last = 77;
    REQUIRE(waitFor([&] {
        if (auto f = worker.takeUpTo(1'000)) {
            last = f->index;
        }
        return worker.finished();
    }));
    CHECK(last == 119);
}

TEST_CASE("DecodeWorker informa si el archivo no se puede abrir", "[media]") {
    std::atomic<bool> failed = false;
    DecodeWorker::Callbacks callbacks;
    callbacks.failed = [&](const std::string&) { failed = true; };
    DecodeWorker worker("no-existe-sharkcut.mp4", HwAccel::None, callbacks);
    REQUIRE(waitFor([&] { return failed.load(); }));
}

TEST_CASE("VideoDecoder guarda los fotogramas anteriores al destino", "[media]") {
    TempClip clip({.frames = 120, .gopSize = 30});
    VideoDecoder decoder(clip.path.string(), HwAccel::None);

    decoder.seekToFrame(70, 10);
    auto frame = decoder.nextFrame();
    REQUIRE(frame.has_value());
    CHECK(frame->index == 70);
    std::vector<VideoFrame> kept = decoder.takeKeptFrames();
    REQUIRE(kept.size() == 10);
    for (std::size_t i = 0; i < kept.size(); ++i) {
        CHECK(kept[i].index == 60 + static_cast<std::int64_t>(i));
        CHECK(contentIndex(kept[i]) == kept[i].index);
    }
    CHECK(decoder.takeKeptFrames().empty());

    // Cerca del principio: solo los que hay.
    decoder.seekToFrame(4, 10);
    frame = decoder.nextFrame();
    REQUIRE(frame.has_value());
    CHECK(frame->index == 4);
    kept = decoder.takeKeptFrames();
    REQUIRE(kept.size() == 4);
    CHECK(kept.front().index == 0);
}

TEST_CASE("VideoDecoder sigue decodificando en saltos cortos hacia delante", "[media]") {
    const bool fragmented = GENERATE(false, true);
    CAPTURE(fragmented);
    TempClip clip({.frames = 120, .gopSize = 30, .fragmented = fragmented});
    VideoDecoder decoder(clip.path.string(), HwAccel::None);
    for (int i = 0; i < 10; ++i) {
        REQUIRE(decoder.nextFrame().has_value());
    }
    // Hacia delante (sin volver al clave), otra vez, y hacia atrás.
    for (const std::int64_t target : {40, 45, 46, 44, 100, 119, 3}) {
        CAPTURE(target);
        decoder.seekToFrame(target);
        const auto frame = decoder.nextFrame();
        REQUIRE(frame.has_value());
        CHECK(frame->index == target);
        CHECK(contentIndex(*frame) == target);
    }
    // Dos saltos seguidos sin leer entre medias: vale el último.
    decoder.seekToFrame(20);
    decoder.seekToFrame(25);
    const auto frame = decoder.nextFrame();
    REQUIRE(frame.has_value());
    CHECK(frame->index == 25);
}

TEST_CASE("DecodeWorker va hacia atras desde su cache", "[media]") {
    TempClip clip({.frames = 120, .gopSize = 30, .fragmented = true});
    DecodeWorker worker(clip.path.string(), HwAccel::None, {}, 6);

    worker.seek(80);
    REQUIRE(waitFor([&] { return worker.hasFrame(80); }));
    REQUIRE(worker.takeUpTo(80)->index == 80);

    int fromCache = 0;
    for (std::int64_t target = 79; target >= 50; --target) {
        CAPTURE(target);
        worker.seek(target);
        if (worker.hasFrame(target)) {
            ++fromCache; // servido al instante, sin decodificar
        }
        REQUIRE(waitFor([&] { return worker.hasFrame(target); }));
        const auto frame = worker.takeUpTo(target);
        REQUIRE(frame);
        CHECK(frame->index == target);
        CHECK(contentIndex(*frame) == target);
    }
    CHECK(fromCache >= 20);

    // Y de ahí, hacia delante con normalidad.
    for (std::int64_t target = 51; target <= 70; ++target) {
        CAPTURE(target);
        REQUIRE(waitFor([&] { return worker.hasFrame(target); }));
        const auto frame = worker.takeUpTo(target);
        REQUIRE(frame);
        CHECK(frame->index == target);
        CHECK(contentIndex(*frame) == target);
    }
}

TEST_CASE("Los proxies conservan los indices de fotograma", "[media][proxy]") {
    TempClip clip({.frames = 90, .width = 640, .height = 360, .gopSize = 60, .fragmented = true});
    const std::filesystem::path proxy = tempClipPath("proxy.mp4");
    std::int64_t lastDone = 0;
    writeProxy(clip.path.string(), proxy.string(), [&](std::int64_t done, std::int64_t total) {
        CHECK(done <= total);
        lastDone = done;
    });
    CHECK(lastDone == 90);
    REQUIRE(std::filesystem::exists(proxy));
    CHECK_FALSE(std::filesystem::exists(proxy.string() + ".tmp"));

    const MediaInfo info = probeMedia(proxy.string());
    REQUIRE(info.video.has_value());
    CHECK(info.video->height <= kProxyHeight);
    CHECK(info.video->frameRate == FrameRate{60, 1});
    CHECK(info.video->frameCount == 90);
    CHECK(info.audio.empty());

    VideoDecoder decoder(proxy.string(), HwAccel::None);
    for (const std::int64_t target : {0, 37, 89, 15, 14}) {
        CAPTURE(target);
        decoder.seekToFrame(target);
        const auto frame = decoder.nextFrame();
        REQUIRE(frame.has_value());
        CHECK(frame->index == target);
        CHECK(contentIndex(*frame) == target);
    }
    std::error_code ignored;
    std::filesystem::remove(proxy, ignored);
}

TEST_CASE("Cancelar un proxy no deja archivos", "[media][proxy]") {
    TempClip clip({.frames = 120});
    const std::filesystem::path proxy = tempClipPath("cancelado.mp4");
    std::atomic<bool> cancel = true;
    CHECK_THROWS_AS(writeProxy(clip.path.string(), proxy.string(), {}, &cancel), MediaError);
    CHECK_FALSE(std::filesystem::exists(proxy));
    CHECK_FALSE(std::filesystem::exists(proxy.string() + ".tmp"));
    CHECK(proxyFileName(clip.path.string()).ends_with(".mp4"));
    CHECK(proxyFileName("/no/existe.mp4").empty());
}
