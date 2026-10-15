// sharkcut_bench: mide lo que más pesa con VODs largos, sin interfaz.
//
// Uso: sharkcut_bench <video> [--cpu] [--fotogramas N] [--saltos N] [--sin-onda]
//                      [--guardar proyecto.sharkp]
//   - Análisis del archivo (probeMedia).
//   - Decodificación seguida (fotogramas por segundo).
//   - Saltos a posiciones al azar (lo que se espera al hacer clic en la regla).
//   - Saltos cortos hacia atrás (como J o al arrastrar el cabezal hacia atrás).
//   - Onda del audio entero.
//   - Edición de una línea de tiempo con miles de clips (dividir, mover, deshacer…).
// Con --cpu se decodifica sin la GPU. Sirve para comparar antes y después de un cambio
// y entre equipos (en el de Kyo, con NVDEC).

#include "core/document.h"
#include "core/edit.h"
#include "core/history.h"
#include "core/project_file.h"
#include "media/decode_worker.h"
#include "media/media_info.h"
#include "media/video_decoder.h"
#include "media/waveform.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace sharkcut;
using Clock = std::chrono::steady_clock;

namespace {

double msSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct Stats {
    std::vector<double> values;
    void add(double v) { values.push_back(v); }
    void print(const char* name) {
        if (values.empty()) {
            return;
        }
        std::sort(values.begin(), values.end());
        double sum = 0;
        for (double v : values) {
            sum += v;
        }
        std::printf("  %-34s media %8.2f ms · mediana %8.2f · máx %8.2f (n=%zu)\n", name,
                    sum / static_cast<double>(values.size()), values[values.size() / 2],
                    values.back(), values.size());
    }
};

void benchTimeline(const MediaItem& item, const std::string& savePath) {
    std::printf("\nLínea de tiempo con muchos clips\n");
    Project p;
    p.settings.frameRate = item.frameRate;
    p.settings.width = item.width;
    p.settings.height = item.height;
    p.settings.adoptCanvas = false;
    p.settings.adoptFrameRate = false;
    const ObjectId vod = addMedia(p, item);
    // Unas 3 h de línea de tiempo (el video repetido) troceadas cada 5 s: unos 2.000
    // cortes en V1/A1 y otros tantos en V2/A2 (la facecam).
    auto start = Clock::now();
    const std::int64_t clipLength = p.mediaFrames(*p.findMedia(vod));
    const std::int64_t wanted = 3 * 3600 * p.settings.frameRate.nominal();
    std::int64_t length = 0;
    while (length < wanted && clipLength > 0) {
        placeClip(p, vod, length);
        placeClip(p, vod, length, PlaceTarget{1, 1});
        length += clipLength;
    }
    const std::int64_t step = 5 * p.settings.frameRate.nominal();
    for (std::int64_t f = step; f < length; f += step) {
        splitAt(p, f);
    }
    std::size_t clips = 0;
    for (const Track& t : p.tracks) {
        clips += t.clips.size();
    }
    std::printf("  %zu clips (preparar: %.0f ms)\n", clips, msSince(start));

    Document doc = documentFromProject(p, "Bench");
    const ObjectId seq = doc.sequences.front().id;
    DocumentHistory history;
    history.reset(doc);
    Stats split, move, ripple, undo, view, json;
    std::mt19937 rng(7);
    std::uniform_int_distribution<std::int64_t> anywhere(1, length - 10);
    for (int i = 0; i < 20; ++i) {
        auto t0 = Clock::now();
        history.apply("dividir", [&](Document& d) {
            return edit(d, seq, [&](Project& v) { return splitAt(v, anywhere(rng)); });
        });
        split.add(msSince(t0));

        t0 = Clock::now();
        history.apply("mover", [&](Document& d) {
            return edit(d, seq, [&](Project& v) {
                const Clip& c = v.tracks[0].clips[v.tracks[0].clips.size() / 2];
                return moveClips(v, {c.id}, 3);
            });
        });
        move.add(msSince(t0));

        t0 = Clock::now();
        history.apply("rizo", [&](Document& d) {
            return edit(d, seq, [&](Project& v) { return rippleTrimNext(v, anywhere(rng)); });
        });
        ripple.add(msSince(t0));

        t0 = Clock::now();
        history.undo();
        undo.add(msSince(t0));

        t0 = Clock::now();
        const Project copy = history.state().view(seq);
        view.add(msSince(t0));
        (void)copy;
    }
    auto t0 = Clock::now();
    const std::string text = documentToJson(history.state(), {}, {});
    json.add(msSince(t0));
    if (!savePath.empty()) {
        saveDocument(history.state(), {{seq}, seq}, pathFromUtf8(savePath));
        std::printf("  guardado en %s\n", savePath.c_str());
    }
    split.print("dividir (con historial)");
    move.print("mover un clip");
    ripple.print("recortar con rizo (W)");
    undo.print("deshacer");
    view.print("vista de la secuencia (copia)");
    json.print("convertir a JSON para guardar");
    std::printf("  proyecto: %.1f MB de JSON\n", static_cast<double>(text.size()) / 1e6);
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr, "Uso: %s <video> [--cpu] [--fotogramas N] [--saltos N] [--sin-onda]\n",
                     argv[0]);
        return 2;
    }
    const std::string path = argv[1];
    HwAccel accel = HwAccel::Auto;
    int frames = 600;
    int seeks = 30;
    bool wave = true;
    std::string savePath;
    for (int i = 2; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--cpu") {
            accel = HwAccel::None;
        } else if (option == "--fotogramas" && i + 1 < argc) {
            frames = std::atoi(argv[++i]);
        } else if (option == "--saltos" && i + 1 < argc) {
            seeks = std::atoi(argv[++i]);
        } else if (option == "--sin-onda") {
            wave = false;
        } else if (option == "--guardar" && i + 1 < argc) {
            savePath = argv[++i]; // el proyecto de prueba, para abrirlo en la app
        }
    }

    try {
        auto t0 = Clock::now();
        const MediaInfo info = probeMedia(path);
        std::printf("Análisis: %.1f ms\n", msSince(t0));
        if (!info.video) {
            std::fprintf(stderr, "El archivo no tiene video\n");
            return 1;
        }
        const VideoStreamInfo& v = *info.video;
        std::printf("  %dx%d · %lld/%lld fps · %lld fotogramas · %s · %s\n", v.width, v.height,
                    static_cast<long long>(v.frameRate.num),
                    static_cast<long long>(v.frameRate.den), static_cast<long long>(v.frameCount),
                    v.codec.c_str(), v.pixelFormat.c_str());

        t0 = Clock::now();
        VideoDecoder decoder(path, accel);
        std::printf("Abrir el decodificador: %.1f ms\n", msSince(t0));

        // Decodificación seguida
        t0 = Clock::now();
        int decoded = 0;
        while (decoded < frames && decoder.nextFrame()) {
            ++decoded;
        }
        const double seconds = msSince(t0) / 1000.0;
        std::printf("Decodificación seguida: %d fotogramas en %.2f s → %.0f fps (%s)\n", decoded,
                    seconds, decoded / std::max(seconds, 1e-9),
                    decoder.hwAccelName().empty() ? "CPU" : decoder.hwAccelName().c_str());

        // Saltos al azar
        std::mt19937 rng(42);
        const std::int64_t total = std::max<std::int64_t>(1, v.frameCount);
        std::uniform_int_distribution<std::int64_t> anywhere(0, total - 1);
        Stats far, back, wrong;
        std::printf("Saltos:\n");
        for (int i = 0; i < seeks; ++i) {
            const std::int64_t target = anywhere(rng);
            t0 = Clock::now();
            decoder.seekToFrame(target);
            const auto frame = decoder.nextFrame();
            far.add(msSince(t0));
            if (!frame || frame->index != target) {
                wrong.add(0);
            }
        }
        // Hacia atrás de 1 en 1 (lo peor: cada fotograma obliga a volver al clave).
        std::int64_t position = std::min<std::int64_t>(total - 1, total / 2);
        for (int i = 0; i < seeks; ++i) {
            t0 = Clock::now();
            decoder.seekToFrame(position - i);
            decoder.nextFrame();
            back.add(msSince(t0));
        }
        // Poco hacia delante (arrastrar el cabezal a la derecha): +10 fotogramas.
        Stats ahead;
        decoder.seekToFrame(total / 3);
        decoder.nextFrame();
        for (int i = 1; i <= seeks; ++i) {
            t0 = Clock::now();
            decoder.seekToFrame(total / 3 + i * 10);
            decoder.nextFrame();
            ahead.add(msSince(t0));
        }
        far.print("a una posición al azar");
        ahead.print("10 fotogramas hacia delante");
        back.print("un fotograma hacia atrás (sin caché)");

        // Marcha atrás con DecodeWorker (como J): lo que tarda cada paso en estar listo.
        {
            DecodeWorker worker(path, accel, {}, 8);
            Stats reverse;
            const std::int64_t from = std::min<std::int64_t>(total - 1, total / 2 + 200);
            for (int i = 0; i < 120; ++i) {
                const std::int64_t target = from - i;
                t0 = Clock::now();
                worker.seek(target);
                while (!worker.hasFrame(target)) {
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                }
                worker.takeUpTo(target);
                reverse.add(msSince(t0));
            }
            reverse.print("marcha atrás (DecodeWorker, J)");
            double sum = 0;
            for (double v : reverse.values) {
                sum += v;
            }
            std::printf("  → hacia atrás a unos %.0f fps\n",
                        1000.0 * static_cast<double>(reverse.values.size()) / sum);
        }
        if (!wrong.values.empty()) {
            std::printf("  ¡%zu saltos no llegaron al fotograma pedido!\n", wrong.values.size());
        }

        if (wave && !info.audio.empty()) {
            t0 = Clock::now();
            const Waveform w = computeWaveform(path);
            std::printf("Onda del audio entero: %.0f ms\n", msSince(t0));
        }

        MediaItem item;
        item.path = path;
        item.name = "bench";
        item.durationUs = info.durationUs;
        item.frameRate = v.frameRate;
        item.width = v.width;
        item.height = v.height;
        item.hasVideo = true;
        item.hasAudio = !info.audio.empty();
        benchTimeline(item, savePath);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
    return 0;
}
