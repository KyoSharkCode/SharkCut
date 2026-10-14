#pragma once

#include "core/project.h"
#include "media/audio_source.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace sharkcut {

class AudioDecoder;

// Audio de la línea de tiempo: los clips de la pista de audio en orden y silencio en
// los huecos. Dura lo mismo que la línea de tiempo (también si el final es solo video).
//
// En cada corte se aplica un fundido muy corto (2 ms) para que no se oiga un chasquido;
// si el clip siguiente continúa exactamente el mismo audio (un clip dividido), no.
class TimelineAudioSource : public AudioSource {
public:
    // Trabaja sobre una copia: las ediciones posteriores no le afectan. Los archivos se
    // abren cuando hacen falta; uno que no se pueda abrir aporta silencio.
    // Lanza MediaError si la línea de tiempo no tiene clips de audio.
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
        bool fadeIn = false;
        bool fadeOut = false;
    };

    std::int64_t samplesOf(std::int64_t frames) const;
    AudioDecoder* decoder(ObjectId media);
    void readSegment(const Segment& segment, std::int64_t from, std::int64_t to, float* out);

    Project m_project;
    AudioStreamInfo m_info;
    std::vector<Segment> m_segments;
    std::map<ObjectId, std::unique_ptr<AudioDecoder>> m_decoders;
    std::set<ObjectId> m_unavailable; // medios que no se pudieron abrir
    std::int64_t m_total = 0;
    std::int64_t m_position = 0;
};

} // namespace sharkcut
