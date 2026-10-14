#pragma once

#include "core/project.h"
#include "media/audio_source.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace sharkcut {

class AudioDecoder;

// Audio de la línea de tiempo: la mezcla de todas las pistas de audio que no están
// silenciadas, cada clip con su volumen, y silencio en los huecos. Dura lo mismo que la
// línea de tiempo (también si el final es solo video).
//
// En cada corte se aplica un fundido muy corto (2 ms) para que no se oiga un chasquido;
// si el clip siguiente de la misma pista continúa exactamente el mismo audio (un clip
// dividido) con el mismo volumen, no.
class TimelineAudioSource : public AudioSource {
public:
    // Trabaja sobre una copia: las ediciones posteriores no le afectan. Los archivos se
    // abren cuando hacen falta; uno que no se pueda abrir aporta silencio.
    // Lanza MediaError si no hay clips de audio en pistas que suenen.
    explicit TimelineAudioSource(Project project);
    ~TimelineAudioSource() override;

    const AudioStreamInfo& info() const override { return m_info; }
    std::size_t read(float* out, std::size_t frames) override;
    void seekToSample(std::int64_t sample) override;

    // Longitud del fundido de cada corte, en muestras.
    static constexpr std::int64_t kCutFade = 96;

private:
    struct Segment {
        std::int64_t start = 0; // muestras de la línea de tiempo [start, end)
        std::int64_t end = 0;
        std::int64_t sourceFirst = 0; // muestra del medio que suena en `start`
        ObjectId media = 0;
        float gain = 1.0f;
        bool fadeIn = false;
        bool fadeOut = false;
    };
    // Un decodificador por pista y medio: dos pistas con el mismo archivo no se pisan.
    using DecoderKey = std::pair<std::size_t, ObjectId>;

    std::int64_t samplesOf(std::int64_t frames) const;
    AudioDecoder* decoder(std::size_t track, ObjectId media);
    // Suma el tramo [from, to) del segmento a `out`.
    void mixSegment(std::size_t track, const Segment& segment, std::int64_t from, std::int64_t to,
                    float* out);

    Project m_project;
    AudioStreamInfo m_info;
    std::vector<std::vector<Segment>> m_tracks; // segmentos de cada pista que suena
    std::map<DecoderKey, std::unique_ptr<AudioDecoder>> m_decoders;
    std::set<ObjectId> m_unavailable; // medios que no se pudieron abrir
    std::vector<float> m_scratch;
    std::int64_t m_total = 0;
    std::int64_t m_position = 0;
};

} // namespace sharkcut
