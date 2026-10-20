#include "speech/transcriber.h"

#include "core/project_file.h" // pathFromUtf8

#include <whisper.h>

#if defined(_MSC_VER)
#include <immintrin.h>
#include <intrin.h>
#endif

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <memory>
#include <thread>

namespace sharkcut {

namespace {

constexpr std::int64_t kRate = 16000;        // lo que espera whisper (mono)
constexpr std::int64_t kChunk = kRate * 600; // se transcribe en tramos de 10 min
constexpr std::int64_t kSearch = kRate * 10; // …cortados en lo más silencioso del final
constexpr std::int64_t kWindow = kRate / 10; // ventanas de 100 ms para buscar ese silencio
constexpr double kSilenceRms = 0.002;        // un tramo así de bajo no se transcribe

// Carga del modelo con rutas UTF-8 también en Windows (whisper usa rutas de 8 bits).
struct Loader {
    std::ifstream in;
};

size_t loaderRead(void* context, void* output, size_t size) {
    auto* loader = static_cast<Loader*>(context);
    loader->in.read(static_cast<char*>(output), static_cast<std::streamsize>(size));
    return static_cast<size_t>(loader->in.gcount());
}

bool loaderEof(void* context) {
    return static_cast<Loader*>(context)->in.eof();
}

void loaderClose(void* context) {
    static_cast<Loader*>(context)->in.close();
}

struct ContextDeleter {
    void operator()(whisper_context* c) const { whisper_free(c); }
};
using ContextPtr = std::unique_ptr<whisper_context, ContextDeleter>;

ContextPtr loadModel(const std::string& path) {
    Loader loader;
    loader.in.open(pathFromUtf8(path), std::ios::binary);
    if (!loader.in) {
        throw SpeechError("No se encuentra el modelo de transcripción");
    }
    whisper_model_loader callbacks{&loader, loaderRead, loaderEof, loaderClose};
    whisper_context_params params = whisper_context_default_params();
    params.use_gpu = true; // si se compiló con CUDA o Vulkan
    ContextPtr context(whisper_init_with_params(&callbacks, params));
    if (!context) {
        throw SpeechError("El modelo de transcripción está dañado o no es válido");
    }
    return context;
}

// Índice (en `audio`) donde cortar un tramo: la ventana más silenciosa de los últimos
// segundos, para no partir una palabra.
std::size_t quietestCut(const std::vector<float>& audio, std::size_t end) {
    const std::size_t from = end > static_cast<std::size_t>(kSearch) ? end - kSearch : 0;
    std::size_t best = end;
    double bestEnergy = std::numeric_limits<double>::max();
    for (std::size_t w = from; w + kWindow <= end; w += kWindow / 2) {
        double energy = 0.0;
        for (std::size_t i = w; i < w + kWindow; ++i) {
            energy += static_cast<double>(audio[i]) * audio[i];
        }
        if (energy < bestEnergy) {
            bestEnergy = energy;
            best = w + kWindow / 2;
        }
    }
    return best;
}

bool silent(const float* audio, std::size_t count) {
    double energy = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        energy += static_cast<double>(audio[i]) * audio[i];
    }
    return count == 0 || std::sqrt(energy / static_cast<double>(count)) < kSilenceRms;
}

// Anotaciones que no son habla: [Música], (risas)…
bool isAnnotation(const std::string& text) {
    return text.size() >= 2 && ((text.front() == '[' && text.back() == ']') ||
                                (text.front() == '(' && text.back() == ')'));
}

std::string trimmed(const char* text) {
    std::string s = text ? text : "";
    const auto first = s.find_first_not_of(" \t\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = s.find_last_not_of(" \t\n");
    return s.substr(first, last - first + 1);
}

struct Progress {
    const TranscribeOptions* options;
    double base = 0.0;  // parte ya hecha (0–1)
    double share = 0.0; // parte que ocupa el tramo en curso
};

} // namespace

bool speechCpuSupported() {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    int info[4] = {};
    __cpuid(info, 1);
    const bool fma = (info[2] & (1 << 12)) != 0;
    const bool osxsave = (info[2] & (1 << 27)) != 0;
    const bool avx = (info[2] & (1 << 28)) != 0;
    const bool f16c = (info[2] & (1 << 29)) != 0;
    // El sistema guarda los registros AVX (XMM e YMM) al cambiar de hilo.
    if (!osxsave || !avx || !fma || !f16c || (_xgetbv(0) & 0x6) != 0x6) {
        return false;
    }
    __cpuidex(info, 7, 0);
    return (info[1] & (1 << 5)) != 0; // AVX2
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma") &&
           __builtin_cpu_supports("f16c");
#else
    return true; // otras arquitecturas: ggml se compila para ellas sin estas extensiones
#endif
}

std::vector<TimedWord> transcribe(const std::string& modelPath, AudioSource& source,
                                  std::int64_t samples, const TranscribeOptions& options) {
    if (!speechCpuSupported()) {
        throw SpeechError("Este procesador no tiene AVX2, que hace falta para transcribir");
    }
    const ContextPtr context = loadModel(modelPath);
    const auto isCancelled = [&options] { return options.cancelled && options.cancelled(); };

    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    const int cores = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    params.n_threads = options.threads > 0 ? options.threads : std::clamp(cores - 1, 1, 8);
    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.print_special = false;
    // Una palabra por segmento, con su momento.
    params.token_timestamps = true;
    params.max_len = 1;
    params.split_on_word = true;
    params.suppress_nst = true; // sin [Música] y similares
    params.language = options.language.empty() ? "auto" : options.language.c_str();

    Progress progress{&options};
    params.progress_callback = [](whisper_context*, whisper_state*, int percent, void* data) {
        const auto* p = static_cast<Progress*>(data);
        if (p->options->progress) {
            p->options->progress(p->base + p->share * percent / 100.0);
        }
    };
    params.progress_callback_user_data = &progress;
    params.abort_callback = [](void* data) {
        const auto* o = static_cast<const TranscribeOptions*>(data);
        return o->cancelled && o->cancelled();
    };
    params.abort_callback_user_data = const_cast<TranscribeOptions*>(&options);

    const std::int64_t total16 = std::max<std::int64_t>(1, samples / 3);
    std::vector<TimedWord> words;
    std::vector<float> pending; // audio a 16 kHz aún sin transcribir
    std::int64_t consumed = 0;  // muestras (16 kHz) ya transcritas
    std::int64_t remaining = samples;
    std::vector<float> block(4800 * 2);
    float sum = 0.0f; // promedio de 3 muestras de 48 kHz → 1 de 16 kHz
    int count = 0;

    const auto runChunk = [&](std::size_t length) {
        progress.base = static_cast<double>(consumed) / total16;
        progress.share = static_cast<double>(length) / total16;
        if (!silent(pending.data(), length)) {
            const int result =
                whisper_full(context.get(), params, pending.data(), static_cast<int>(length));
            if (result != 0 && !isCancelled()) {
                throw SpeechError("La transcripción falló (código " + std::to_string(result) + ")");
            }
            const std::int64_t offsetUs = consumed * 1'000'000 / kRate;
            const int segments = whisper_full_n_segments(context.get());
            for (int i = 0; i < segments && result == 0; ++i) {
                const std::string text = trimmed(whisper_full_get_segment_text(context.get(), i));
                if (text.empty() || isAnnotation(text)) {
                    continue;
                }
                // Tiempos en centésimas de segundo desde el inicio del tramo.
                const std::int64_t t0 = whisper_full_get_segment_t0(context.get(), i);
                const std::int64_t t1 = whisper_full_get_segment_t1(context.get(), i);
                words.push_back(
                    {offsetUs + t0 * 10'000, offsetUs + std::max(t1, t0) * 10'000, text});
            }
        }
        pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(length));
        consumed += static_cast<std::int64_t>(length);
        if (options.progress) {
            options.progress(static_cast<double>(consumed) / total16);
        }
    };

    while (remaining > 0 && !isCancelled()) {
        const std::size_t want = static_cast<std::size_t>(std::min<std::int64_t>(remaining, 4800));
        const std::size_t got = source.read(block.data(), want);
        if (got == 0) {
            break;
        }
        remaining -= static_cast<std::int64_t>(got);
        for (std::size_t i = 0; i < got; ++i) {
            sum += 0.5f * (block[i * 2] + block[i * 2 + 1]);
            if (++count == 3) {
                pending.push_back(sum / 3.0f);
                sum = 0.0f;
                count = 0;
            }
        }
        if (static_cast<std::int64_t>(pending.size()) >= kChunk) {
            runChunk(quietestCut(pending, static_cast<std::size_t>(kChunk)));
        }
    }
    if (!isCancelled() && !pending.empty()) {
        runChunk(pending.size());
    }
    return words;
}

} // namespace sharkcut
