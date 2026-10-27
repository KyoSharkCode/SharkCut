#include "core/project.h"
#include "media/chroma_key.h"
#include "media/decode_worker.h"
#include "media/media_error.h"
#include "media/media_info.h"
#include "media/proxy.h"
#include "media/video_decoder.h"
#include "test_clip.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>
#include <utility>

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

TEST_CASE("Los videos AV1 se decodifican y saltan al fotograma exacto", "[media][av1]") {
    // Como graba OBS con AV1: MP4 fragmentado o MKV.
    const std::string container = GENERATE("mp4", "matroska");
    CAPTURE(container);
    std::string encoder;
    for (const char* name : {"libsvtav1", "libaom-av1"}) {
        if (avcodec_find_encoder_by_name(name)) {
            encoder = name;
            break;
        }
    }
    if (encoder.empty()) {
        SKIP("FFmpeg sin codificador AV1");
    }
    TempClip clip({.frames = 90,
                   .fragmented = container == "mp4",
                   .container = container,
                   .videoEncoder = encoder},
                  container == "mp4" ? "av1.mp4" : "av1.mkv");
    REQUIRE(clip.codec == encoder);

    const MediaInfo info = probeMedia(clip.path.string());
    REQUIRE(info.video.has_value());
    CHECK(info.video->codec == "av1");

    // Auto: sin GPU compatible (como aquí) se usa dav1d por software.
    for (const HwAccel accel : {HwAccel::None, HwAccel::Auto}) {
        VideoDecoder decoder(clip.path.string(), accel);
        for (int i = 0; i < 90; ++i) {
            const auto frame = decoder.nextFrame();
            REQUIRE(frame.has_value());
            REQUIRE(frame->index == i);
            REQUIRE(contentIndex(*frame) == i);
        }
        CHECK_FALSE(decoder.nextFrame().has_value());
        for (const int target : {57, 3, 89, 30}) {
            decoder.seekToFrame(target);
            const auto frame = decoder.nextFrame();
            REQUIRE(frame.has_value());
            CHECK(frame->index == target);
            CHECK(contentIndex(*frame) == target);
        }
    }
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

TEST_CASE("VideoDecoder seekNear entrega el fotograma clave anterior", "[media]") {
    TempClip clip({.frames = 120, .gopSize = 30});
    VideoDecoder decoder(clip.path.string(), HwAccel::None);
    for (const std::int64_t target : {57, 0, 119, 30, 89}) {
        CAPTURE(target);
        decoder.seekNear(target);
        const auto frame = decoder.nextFrame();
        REQUIRE(frame.has_value());
        CHECK(frame->index <= target);
        CHECK(frame->index > target - 30);
        CHECK(contentIndex(*frame) == frame->index);
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

TEST_CASE("DecodeWorker sabe si sigue justo despues de un fotograma", "[media]") {
    TempClip clip({.frames = 60});
    DecodeWorker worker(clip.path.string(), HwAccel::None, {}, 4);
    worker.seek(10);
    REQUIRE(waitFor([&] { return worker.hasFrame(12); }));
    auto frame = worker.takeUpTo(10);
    REQUIRE(frame);
    CHECK(worker.continuesAfter(10)); // el 11 está en la cola
    CHECK_FALSE(worker.continuesAfter(30));
    // Vaciar la cola: lo siguiente es el que se decodifica tras el último entregado.
    std::int64_t last = 10;
    REQUIRE(waitFor([&] {
        if (auto f = worker.takeUpTo(1'000)) {
            last = f->index;
        }
        return last >= 20;
    }));
    CHECK(worker.continuesAfter(last));
    CHECK_FALSE(worker.continuesAfter(last - 5));
    worker.seek(40);
    CHECK_FALSE(worker.continuesAfter(last));
}

TEST_CASE("A solid color is decoded as a still frame without a file", "[media]") {
    const SolidColor white{0xFFFFFF, 1080, 1920};
    const std::string path = solidColorPath(white);
    CHECK(path == "sharkcut-color:FFFFFF:1080x1920");
    CHECK(solidColorOf(path) == white);
    CHECK_FALSE(solidColorOf("/medios/video.mp4").has_value());
    CHECK_FALSE(solidColorOf("sharkcut-color:ZZZ:1x1").has_value());
    CHECK_FALSE(solidColorOf("sharkcut-color:FFFFFF:0x10").has_value());

    VideoDecoder decoder(path, HwAccel::Auto);
    CHECK(decoder.info().still);
    CHECK(decoder.info().width == 1080);
    auto frame = decoder.nextFrame();
    REQUIRE(frame);
    CHECK(frame->index == 0);
    CHECK(frame->width == 36); // la forma del lienzo, en pequeño
    CHECK(frame->height == 64);
    CHECK(frame->y.front() == 255);
    CHECK(frame->uv.front() == 128);
    CHECK_FALSE(decoder.nextFrame()); // un solo fotograma…
    decoder.seekToFrame(500);
    CHECK(decoder.nextFrame()); // …que es el de cualquier momento

    VideoDecoder red(solidColorPath({0xFF0000, 1920, 1080}), HwAccel::None);
    frame = red.nextFrame();
    REQUIRE(frame);
    CHECK(frame->fullRange);
    CHECK(frame->y.front() == 54); // 0,2126 × 255
    CHECK(frame->uv[1] == 255);    // Cr al máximo
}

TEST_CASE("DecodeWorker says when it is waiting with a full queue", "[media]") {
    TempClip clip({.frames = 60});
    DecodeWorker worker(clip.path.string(), HwAccel::None, {}, 4);
    worker.seek(20);
    // Con la cola llena se para a esperar: es lo que el visor mira para saber si un clip
    // sin su fotograma está atascado (y no solo lento).
    REQUIRE(waitFor([&] {
        const DecodeWorker::Status s = worker.status();
        return s.waiting && s.firstQueued == 20 && s.lastQueued == 23;
    }));
    const DecodeWorker::Status full = worker.status();
    CHECK(full.opened);
    CHECK(full.seekPending < 0);
    CHECK(full.nextIndex == 24);
    // Al sacar fotogramas vuelve a decodificar.
    REQUIRE(worker.takeUpTo(21));
    REQUIRE(waitFor([&] { return worker.status().lastQueued == 25; }));
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

namespace {

// .mov con transparencia: mitad izquierda transparente, mitad derecha opaca y gris.
// `codec`: "qtrle" (Animación, ARGB) o "prores_ks" (ProRes 4444, YUVA 10 bits).
std::filesystem::path writeAlphaClip(const char* codecName, int frames = 10) {
    const std::filesystem::path path = tempClipPath(std::string("alfa-") + codecName + ".mov");
    const AVCodec* codec = avcodec_find_encoder_by_name(codecName);
    if (!codec) {
        return {};
    }
    AVFormatContext* out = nullptr;
    avformat_alloc_output_context2(&out, nullptr, "mov", path.string().c_str());
    AVStream* stream = avformat_new_stream(out, nullptr);
    AVCodecContext* enc = avcodec_alloc_context3(codec);
    const bool prores = std::string(codecName) == "prores_ks";
    enc->width = 64;
    enc->height = 32;
    enc->time_base = {1, 30};
    enc->framerate = {30, 1};
    enc->pix_fmt = prores ? AV_PIX_FMT_YUVA444P10LE : AV_PIX_FMT_ARGB;
    if (prores) {
        enc->profile = 4; // 4444
    }
    if (out->oformat->flags & AVFMT_GLOBALHEADER) {
        enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    REQUIRE(avcodec_open2(enc, codec, nullptr) >= 0);
    avcodec_parameters_from_context(stream->codecpar, enc);
    stream->time_base = enc->time_base;
    REQUIRE(avio_open(&out->pb, path.string().c_str(), AVIO_FLAG_WRITE) >= 0);
    REQUIRE(avformat_write_header(out, nullptr) >= 0);
    AVFrame* frame = av_frame_alloc();
    frame->format = enc->pix_fmt;
    frame->width = 64;
    frame->height = 32;
    av_frame_get_buffer(frame, 0);
    AVPacket* packet = av_packet_alloc();
    auto drain = [&] {
        while (avcodec_receive_packet(enc, packet) == 0) {
            av_packet_rescale_ts(packet, enc->time_base, stream->time_base);
            packet->stream_index = stream->index;
            av_interleaved_write_frame(out, packet);
        }
    };
    for (int i = 0; i < frames; ++i) {
        av_frame_make_writable(frame);
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 64; ++x) {
                const bool opaque = x >= 32;
                if (prores) {
                    auto plane = [&](int p) {
                        return reinterpret_cast<std::uint16_t*>(frame->data[p] +
                                                                y * frame->linesize[p]);
                    };
                    plane(0)[x] = static_cast<std::uint16_t>(4 * 160); // Y (limitado)
                    plane(1)[x] = 512;
                    plane(2)[x] = 512;
                    plane(3)[x] = opaque ? 1023 : 0;
                } else {
                    std::uint8_t* px = frame->data[0] + y * frame->linesize[0] + x * 4;
                    px[0] = opaque ? 255 : 0; // A
                    px[1] = px[2] = px[3] = 180;
                }
            }
        }
        frame->pts = i;
        avcodec_send_frame(enc, frame);
        drain();
    }
    avcodec_send_frame(enc, nullptr);
    drain();
    av_write_trailer(out);
    avio_closep(&out->pb);
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&enc);
    avformat_free_context(out);
    return path;
}

} // namespace

TEST_CASE("Los mov con transparencia conservan su alfa", "[media][alfa]") {
    const char* codec = GENERATE("qtrle", "prores_ks");
    CAPTURE(codec);
    const std::filesystem::path path = writeAlphaClip(codec);
    if (path.empty()) {
        SKIP("FFmpeg sin ese codificador");
    }
    {
        VideoDecoder decoder(path.string(), HwAccel::None);
        const auto frame = decoder.nextFrame();
        REQUIRE(frame.has_value());
        REQUIRE(frame->hasAlpha());
        REQUIRE(frame->a.size() == static_cast<std::size_t>(64 * 32));
        const auto at = [&](int x, int y) {
            return frame->a[static_cast<std::size_t>(y * 64 + x)];
        };
        CHECK(at(4, 16) <= 2);    // transparente
        CHECK(at(60, 16) >= 253); // opaco
        // Gris → luma de rango limitado (Animación: 180 en RGB ≈ 170; ProRes: 160).
        const int luma = frame->y[static_cast<std::size_t>(16 * 64 + 60)];
        CHECK(std::abs(luma - (std::string(codec) == "qtrle" ? 170 : 160)) <= 3);
        CHECK_FALSE(frame->fullRange);

        // Sin proxy: perdería la transparencia.
        const std::filesystem::path proxy = tempClipPath("proxy-alfa.mp4");
        CHECK_THROWS_AS(writeProxy(path.string(), proxy.string()), MediaError);
        CHECK_FALSE(std::filesystem::exists(proxy));
    }
    // Un video normal no lleva plano de alfa.
    TempClip opaque({.frames = 5});
    VideoDecoder plain(opaque.path.string(), HwAccel::None);
    const auto frame = plain.nextFrame();
    REQUIRE(frame.has_value());
    CHECK_FALSE(frame->hasAlpha());
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

namespace {

// Imagen de 64×32 de un solo fotograma: "png" (RGBA, mitad izquierda transparente y la
// derecha opaca y gris 180) o "mjpeg" (JPG gris 180). El paquete del codificador es el
// archivo entero. Ruta vacía si FFmpeg no tiene ese codificador.
std::filesystem::path writeImage(const char* codecName, const std::string& name) {
    const AVCodec* codec = avcodec_find_encoder_by_name(codecName);
    if (!codec) {
        return {};
    }
    const bool png = std::string(codecName) == "png";
    AVCodecContext* enc = avcodec_alloc_context3(codec);
    enc->width = 64;
    enc->height = 32;
    enc->time_base = {1, 25};
    enc->pix_fmt = png ? AV_PIX_FMT_RGBA : AV_PIX_FMT_YUVJ420P;
    REQUIRE(avcodec_open2(enc, codec, nullptr) >= 0);
    AVFrame* frame = av_frame_alloc();
    frame->format = enc->pix_fmt;
    frame->width = 64;
    frame->height = 32;
    av_frame_get_buffer(frame, 0);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 64; ++x) {
            if (png) {
                std::uint8_t* px = frame->data[0] + y * frame->linesize[0] + x * 4;
                px[0] = px[1] = px[2] = 180;
                px[3] = x >= 32 ? 255 : 0;
            } else {
                frame->data[0][y * frame->linesize[0] + x] = 180;
                if (x % 2 == 0 && y % 2 == 0) {
                    frame->data[1][y / 2 * frame->linesize[1] + x / 2] = 128;
                    frame->data[2][y / 2 * frame->linesize[2] + x / 2] = 128;
                }
            }
        }
    }
    frame->pts = 0;
    AVPacket* packet = av_packet_alloc();
    REQUIRE(avcodec_send_frame(enc, frame) >= 0);
    avcodec_send_frame(enc, nullptr);
    REQUIRE(avcodec_receive_packet(enc, packet) == 0);
    const std::filesystem::path path = tempClipPath(name);
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(packet->data), packet->size);
    }
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&enc);
    return path;
}

} // namespace

TEST_CASE("Las imagenes PNG y JPG se leen como imagen fija", "[media][imagen]") {
    const auto [codec, name] =
        GENERATE(std::pair{"png", "imagen.png"}, std::pair{"mjpeg", "imagen.jpg"});
    CAPTURE(name);
    const std::filesystem::path path = writeImage(codec, name);
    if (path.empty()) {
        SKIP("FFmpeg sin ese codificador");
    }
    const bool png = std::string(codec) == "png";

    const MediaInfo info = probeMedia(path.string());
    REQUIRE(info.video);
    CHECK(info.video->still);
    CHECK(info.video->frameCount == 1);
    CHECK(info.video->width == 64);

    {
        VideoDecoder decoder(path.string(), HwAccel::None);
        auto frame = decoder.nextFrame();
        REQUIRE(frame.has_value());
        CHECK(frame->index == 0);
        CHECK(frame->hasAlpha() == png);
        if (png) {
            CHECK(frame->a[std::size_t{16 * 64 + 4}] <= 2);    // transparente
            CHECK(frame->a[std::size_t{16 * 64 + 60}] >= 253); // opaca
        }
        // Gris 180 → luma de rango limitado (≈ 170).
        CHECK(std::abs(frame->y[std::size_t{16 * 64 + 60}] - 170) <= 4);
        CHECK_FALSE(decoder.nextFrame().has_value()); // un solo fotograma
        // Cualquier salto vuelve a dar la imagen (sin tocar el archivo).
        decoder.seekToFrame(500);
        frame = decoder.nextFrame();
        REQUIRE(frame.has_value());
        CHECK(frame->index == 0);
        decoder.seekNear(30);
        CHECK(decoder.nextFrame().has_value());

        // Sin proxy: no le hace falta.
        DecodeWorker worker(path.string(), HwAccel::None, {}, 4);
        worker.seek(0);
        REQUIRE(waitFor([&] { return worker.hasFrame(0); }));
        auto shown = worker.takeUpTo(0);
        REQUIRE(shown);
        CHECK(shown->width == 64);
        worker.seek(0); // otra vez (p. ej. al volver al clip)
        REQUIRE(waitFor([&] { return worker.hasFrame(0); }));
    }
    // Un video no es una imagen fija.
    TempClip video({.frames = 5});
    CHECK_FALSE(probeMedia(video.path.string()).video->still);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

namespace {

// Fotograma NV12 (BT.709, rango limitado) con el color RGB (0–255) que da `paint`.
template <typename Paint> VideoFrame paintFrame(int width, int height, Paint paint) {
    VideoFrame f;
    f.width = width;
    f.height = height;
    f.y.resize(static_cast<std::size_t>(width) * height);
    f.uv.resize(static_cast<std::size_t>(f.uvWidth()) * 2 * f.uvHeight());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto [r8, g8, b8] = paint(x, y);
            const double r = r8 / 255.0, g = g8 / 255.0, b = b8 / 255.0;
            const double luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            f.y[static_cast<std::size_t>(y) * width + x] =
                static_cast<std::uint8_t>(std::lround(16 + 219 * luma));
            if (x % 2 == 0 && y % 2 == 0) {
                const std::size_t i = (static_cast<std::size_t>(y / 2) * f.uvWidth() + x / 2) * 2;
                f.uv[i] = static_cast<std::uint8_t>(std::lround(128 + 224 * (b - luma) / 1.8556));
                f.uv[i + 1] =
                    static_cast<std::uint8_t>(std::lround(128 + 224 * (r - luma) / 1.5748));
            }
        }
    }
    return f;
}

struct Rgb8 {
    int r, g, b;
};

Rgb8 split(std::uint32_t color) {
    return {static_cast<int>(color >> 16), static_cast<int>((color >> 8) & 0xFF),
            static_cast<int>(color & 0xFF)};
}

} // namespace

TEST_CASE("detectKeyColor finds the background of a green screen", "[media][croma]") {
    // Fondo verde con una sombra abajo y una persona (tono piel) en el centro que llega
    // hasta el borde inferior, como un busto.
    const VideoFrame frame = paintFrame(320, 180, [](int x, int y) {
        if (x > 110 && x < 210 && y > 40) {
            return std::array<int, 3>{220, 170, 140};
        }
        if (y > 150) {
            return std::array<int, 3>{30, 120, 50}; // sombra
        }
        return std::array<int, 3>{40, 180, 70};
    });
    const auto key = detectKeyColor(frame);
    REQUIRE(key.has_value());
    const Rgb8 c = split(*key);
    CHECK(c.g > c.r + 80);
    CHECK(c.g > c.b + 60);

    // Pantalla azul.
    const auto blue = detectKeyColor(
        paintFrame(160, 90, [](int, int) { return std::array<int, 3>{20, 60, 200}; }));
    REQUIRE(blue.has_value());
    CHECK(split(*blue).b > 150);

    // Bordes grises: no hay fondo de color que quitar.
    CHECK_FALSE(detectKeyColor(paintFrame(160, 90, [](int x, int) {
                    const int v = 40 + x;
                    return std::array<int, 3>{v, v, v};
                })).has_value());
}
