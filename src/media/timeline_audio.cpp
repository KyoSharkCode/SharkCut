#include "media/timeline_audio.h"

#include "media/audio_decoder.h"
#include "media/media_error.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>

namespace sharkcut {

namespace {

// Cuánto antes de que suene un clip se abre su archivo (2 s a 48 kHz).
constexpr std::int64_t kOpenAhead = 2 * kAudioSampleRate;

} // namespace

TimelineAudioSource::TimelineAudioSource(Project project) : m_project(std::move(project)) {
    if (!m_project.hasAudibleAudio()) {
        throw MediaError("La línea de tiempo no tiene audio");
    }
    m_info.streamIndex = 0;
    m_info.codec = "mezcla";
    m_info.sampleRate = kAudioSampleRate;
    m_info.channels = kAudioChannels;

    m_tracks.resize(m_project.tracks.size());
    for (std::size_t t = 0; t < m_project.tracks.size(); ++t) {
        const Track& track = m_project.tracks[t];
        if (track.kind != TrackKind::Audio || track.muted) {
            continue;
        }
        std::vector<Segment>& segments = m_tracks[t];
        for (const Clip& clip : track.clips) {
            Segment segment;
            segment.start = samplesOf(clip.start);
            segment.end = samplesOf(clip.end());
            segment.sourceFirst = samplesOf(clip.in);
            segment.media = clip.media;
            segment.gain = static_cast<float>(gainFromDb(clip.volumeDb));
            if (segment.end > segment.start) {
                segments.push_back(segment);
            }
        }
        // Fundidos en los cortes, salvo donde el audio continúa sin interrupción.
        for (std::size_t i = 0; i < segments.size(); ++i) {
            Segment& s = segments[i];
            const bool continuesPrevious =
                i > 0 && segments[i - 1].end == s.start && segments[i - 1].media == s.media &&
                segments[i - 1].gain == s.gain &&
                segments[i - 1].sourceFirst + (segments[i - 1].end - segments[i - 1].start) ==
                    s.sourceFirst;
            s.fadeIn = !continuesPrevious;
            if (i > 0) {
                segments[i - 1].fadeOut = !continuesPrevious;
            }
        }
        if (!segments.empty()) {
            segments.back().fadeOut = true;
        }
    }
    m_total = samplesOf(m_project.length());
}

TimelineAudioSource::~TimelineAudioSource() = default;

std::int64_t TimelineAudioSource::samplesOf(std::int64_t frames) const {
    return rescale(frames, m_project.settings.frameRate.frameDuration(), kAudioSampleDuration);
}

AudioDecoder* TimelineAudioSource::decoder(std::size_t track, ObjectId media) {
    const DecoderKey key{track, media};
    if (const auto it = m_decoders.find(key); it != m_decoders.end()) {
        return it->second.get();
    }
    if (m_unavailable.contains(media)) {
        return nullptr;
    }
    const MediaItem* item = m_project.findMedia(media);
    try {
        if (item) {
            // Se abre al llegar a su primer clip, en el hilo del audio: si tarda, el
            // audio se queda sin muestras (queda en el registro de la sesión).
            const auto began = std::chrono::steady_clock::now();
            auto opened = std::make_unique<AudioDecoder>(item->path);
            const auto ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began)
                    .count();
            std::fprintf(stderr, "Audio de la mezcla: «%s» abierto en %.1f ms\n",
                         item->name.c_str(), ms);
            AudioDecoder* result = opened.get();
            m_decoders.emplace(key, std::move(opened));
            return result;
        }
    } catch (const std::exception&) {
        // archivo movido o sin audio: silencio
    }
    m_unavailable.insert(media);
    return nullptr;
}

void TimelineAudioSource::seekToSample(std::int64_t sample) {
    m_position = std::clamp<std::int64_t>(sample, 0, m_total);
}

void TimelineAudioSource::mixSegment(std::size_t track, const Segment& segment, std::int64_t from,
                                     std::int64_t to, float* out) {
    AudioDecoder* source = decoder(track, segment.media);
    if (!source || segment.gain <= 0.0f) {
        return; // silencio: no suma nada
    }
    const std::int64_t wanted = segment.sourceFirst + (from - segment.start);
    if (source->position() != wanted) {
        source->seekToSample(wanted);
    }
    const auto count = static_cast<std::size_t>(to - from);
    m_scratch.assign(count * kAudioChannels, 0.0f);
    std::size_t remaining = count;
    float* cursor = m_scratch.data();
    while (remaining > 0) {
        const std::size_t got = source->read(cursor, remaining);
        if (got == 0) {
            break; // el medio se acaba antes que el clip: el resto, silencio
        }
        remaining -= got;
        cursor += got * kAudioChannels;
    }

    // Volumen del clip y fundidos de los cortes.
    for (std::int64_t t = from; t < to; ++t) {
        float gain = segment.gain;
        if (segment.fadeIn && t - segment.start < kCutFade) {
            gain *= (static_cast<float>(t - segment.start) + 0.5f) / static_cast<float>(kCutFade);
        }
        if (segment.fadeOut && segment.end - t <= kCutFade) {
            gain *= (static_cast<float>(segment.end - t) - 0.5f) / static_cast<float>(kCutFade);
        }
        const auto offset = static_cast<std::size_t>(t - from) * kAudioChannels;
        for (int c = 0; c < kAudioChannels; ++c) {
            out[offset + static_cast<std::size_t>(c)] +=
                m_scratch[offset + static_cast<std::size_t>(c)] * gain;
        }
    }
}

std::size_t TimelineAudioSource::read(float* out, std::size_t frames) {
    const auto n = static_cast<std::size_t>(
        std::clamp<std::int64_t>(m_total - m_position, 0, static_cast<std::int64_t>(frames)));
    if (n == 0) {
        return 0;
    }
    const std::int64_t start = m_position;
    const std::int64_t end = start + static_cast<std::int64_t>(n);
    std::memset(out, 0, n * kAudioChannels * sizeof(float));

    for (std::size_t track = 0; track < m_tracks.size(); ++track) {
        const std::vector<Segment>& segments = m_tracks[track];
        // Primer segmento que termina después de `start`.
        auto it = std::upper_bound(segments.begin(), segments.end(), start,
                                   [](std::int64_t t, const Segment& s) { return t < s.end; });
        for (; it != segments.end() && it->start < end; ++it) {
            const std::int64_t from = std::max(start, it->start);
            const std::int64_t to = std::min(end, it->end);
            mixSegment(track, *it, from, to,
                       out + static_cast<std::size_t>(from - start) * kAudioChannels);
        }
        // Los archivos de lo que va a sonar pronto se abren ya, con margen: abrirlos al
        // llegar a su clip dejaba al audio sin muestras un momento (y el visor se paraba).
        for (; it != segments.end() && it->start < end + kOpenAhead; ++it) {
            decoder(track, it->media);
        }
    }
    m_position = end;
    return n;
}

} // namespace sharkcut
