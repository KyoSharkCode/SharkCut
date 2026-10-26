#pragma once

// Transcripción local con whisper.cpp: audio → palabras con su momento, para crear
// subtítulos. Funciona sin internet; solo el modelo se descarga una vez.

#include "core/captions.h"
#include "media/audio_source.h"

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace sharkcut {

// Modelos de whisper que usa SharkCut (de huggingface.co/ggerganov/whisper.cpp).
struct SpeechModel {
    const char* file;
    std::int64_t size;  // bytes
    const char* sha256; // del archivo entero, en hexadecimal
};
inline constexpr SpeechModel kSpeechModelAccurate{
    "ggml-large-v3-turbo-q5_0.bin", 574041195,
    "394221709cd5ad1f40c46e6031ca61bce88931e6e088c188294c6d5a55ffa7e2"};
inline constexpr SpeechModel kSpeechModelFast{
    "ggml-small-q5_1.bin", 190085487,
    "ae85e4a935d7a567bd102fe55afc16bb595bdb618e11b2fc7591bc08120411bb"};
inline constexpr const char* kSpeechModelBaseUrl =
    "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/";

struct TranscribeOptions {
    std::string language = "es";          // código ISO 639-1, o "auto" para detectarlo
    int threads = 0;                      // 0: según el procesador
    std::function<void(double)> progress; // 0–1
    std::function<bool()> cancelled;
};

class SpeechError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// El modelo ya está descargado en `folder` (ruta UTF-8): su archivo existe y mide lo que
// debe (una descarga cortada se queda en un temporal). Mira el disco: fuera del hilo de
// la interfaz.
bool isSpeechModelDownloaded(const std::string& folder, const SpeechModel& model);

// El procesador tiene lo que necesita la transcripción (AVX2, FMA y F16C, con soporte del
// sistema operativo). Sin ello, whisper se cerraría con «instrucción no válida».
bool speechCpuSupported();

// Transcribe `samples` muestras (por canal) de `source` (float estéreo a 48 kHz, desde
// su posición actual). Los tiempos son microsegundos desde esa posición. Si se cancela,
// devuelve lo transcrito hasta entonces. Lanza SpeechError si no se puede.
std::vector<TimedWord> transcribe(const std::string& modelPath, AudioSource& source,
                                  std::int64_t samples, const TranscribeOptions& options);

} // namespace sharkcut
