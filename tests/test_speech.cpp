#include "media/audio_decoder.h"
#include "media/media_info.h"
#include "speech/transcriber.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

using namespace sharkcut;

// Necesita un modelo de whisper y una grabación con voz: SHARKCUT_WHISPER_MODEL (p. ej.
// ggml-tiny.bin) y SHARKCUT_WHISPER_SAMPLE (samples/jfk.wav de whisper.cpp). Sin ellos,
// se salta.
TEST_CASE("Transcription returns timed words", "[speech]") {
    const char* model = std::getenv("SHARKCUT_WHISPER_MODEL");
    const char* sample = std::getenv("SHARKCUT_WHISPER_SAMPLE");
    if (!model || !sample) {
        SKIP("Sin SHARKCUT_WHISPER_MODEL y SHARKCUT_WHISPER_SAMPLE");
    }
    AudioDecoder audio(sample);
    const std::int64_t samples = probeMedia(sample).durationUs * 48 / 1000;
    TranscribeOptions options;
    options.language = "en";
    double lastProgress = 0.0;
    options.progress = [&lastProgress](double p) { lastProgress = p; };
    const auto words = transcribe(model, audio, samples, options);

    std::string text;
    for (const TimedWord& w : words) {
        text += w.text + " ";
    }
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    CAPTURE(text);
    CHECK(text.find("country") != std::string::npos);
    CHECK(words.size() > 10);
    // En orden y dentro de la grabación (11 s).
    for (std::size_t i = 1; i < words.size(); ++i) {
        CHECK(words[i].startUs >= words[i - 1].startUs);
    }
    CHECK(words.back().endUs <= 11'500'000);
    CHECK(lastProgress > 0.99);

    // Cancelado desde el principio: nada.
    AudioDecoder again(sample);
    options.cancelled = [] { return true; };
    CHECK(transcribe(model, again, samples, options).empty());

    CHECK_THROWS_AS(transcribe("/no/existe.bin", again, samples, {}), SpeechError);
}
