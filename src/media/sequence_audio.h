#pragma once

#include "core/sequence.h"
#include "core/time.h"
#include "media/audio_source.h"

#include <memory>
#include <string>

namespace sharkcut {

class AudioDecoder;

// Audio de la secuencia A → transición → B: el tramo de A, después el de B, y durante
// la transición un fundido cruzado de potencia constante (el volumen percibido no baja
// a mitad). Un clip sin audio aporta silencio.
class SequenceAudioSource : public AudioSource {
public:
    // Lanza MediaError si ninguno de los dos clips tiene audio.
    SequenceAudioSource(const SequenceLayout& layout, FrameRate rate, const std::string& pathA,
                        const std::string& pathB);
    ~SequenceAudioSource() override;

    const AudioStreamInfo& info() const override;
    std::size_t read(float* out, std::size_t frames) override;
    void seekToSample(std::int64_t sample) override;

private:
    std::int64_t samplesOf(std::int64_t frames) const;
    void readClip(AudioDecoder* decoder, float* out, std::size_t frames);

    SequenceLayout m_layout;
    FrameRate m_rate;
    std::unique_ptr<AudioDecoder> m_a;
    std::unique_ptr<AudioDecoder> m_b;
    AudioStreamInfo m_info;

    // Límites en muestras de la línea de tiempo.
    std::int64_t m_aEnd = 0;   // fin de A (= fin de la transición)
    std::int64_t m_bStart = 0; // inicio de B (= inicio de la transición)
    std::int64_t m_total = 0;
    // Muestra del archivo donde empieza cada tramo.
    std::int64_t m_aFirst = 0;
    std::int64_t m_bFirst = 0;

    std::int64_t m_position = 0;
};

} // namespace sharkcut
