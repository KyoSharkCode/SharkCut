#include "media/timeline_audio.h"

#include "media/audio_decoder.h"
#include "media/media_error.h"

#include <algorithm>
#include <cstring>
#include <exception>

namespace sharkcut {

TimelineAudioSource::TimelineAudioSource(Project project) : m_project(std::move(project)) {
    const auto& clips = m_project.tracks.at(kAudioTrack).clips;
    if (clips.empty()) {
        throw MediaError("La línea de tiempo no tiene audio");
    }
    m_info.streamIndex = 0;
    m_info.codec = "mezcla";
    m_info.sampleRate = kAudioSampleRate;
    m_info.channels = kAudioChannels;

    for (const Clip& clip : clips) {
        Segment segment;
        segment.start = samplesOf(clip.start);
        segment.end = samplesOf(clip.end());
        segment.sourceFirst = samplesOf(clip.in);
        segment.media = clip.media;
        if (segment.end > segment.start) {
            m_segments.push_back(segment);
        }
    }
    // Fundidos en los cortes, salvo donde el audio continúa sin interrupción.
    for (std::size_t i = 0; i < m_segments.size(); ++i) {
        Segment& s = m_segments[i];
        const bool continuesPrevious =
            i > 0 && m_segments[i - 1].end == s.start && m_segments[i - 1].media == s.media &&
            m_segments[i - 1].sourceFirst + (m_segments[i - 1].end - m_segments[i - 1].start) ==
                s.sourceFirst;
        s.fadeIn = !continuesPrevious;
        if (i > 0) {
            m_segments[i - 1].fadeOut = !continuesPrevious;
        }
    }
    if (!m_segments.empty()) {
        m_segments.back().fadeOut = true;
    }
    m_total = samplesOf(m_project.length());
}

TimelineAudioSource::~TimelineAudioSource() = default;

std::int64_t TimelineAudioSource::samplesOf(std::int64_t frames) const {
    return rescale(frames, m_project.settings.frameRate.frameDuration(), kAudioSampleDuration);
}

AudioDecoder* TimelineAudioSource::decoder(ObjectId media) {
    if (const auto it = m_decoders.find(media); it != m_decoders.end()) {
        return it->second.get();
    }
    if (m_unavailable.contains(media)) {
        return nullptr;
    }
    const MediaItem* item = m_project.findMedia(media);
    try {
        if (item) {
            auto opened = std::make_unique<AudioDecoder>(item->path);
            AudioDecoder* result = opened.get();
            m_decoders.emplace(media, std::move(opened));
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

void TimelineAudioSource::readSegment(const Segment& segment, std::int64_t from, std::int64_t to,
                                      float* out) {
    AudioDecoder* source = decoder(segment.media);
    if (!source) {
        return; // ya está en silencio
    }
    const std::int64_t wanted = segment.sourceFirst + (from - segment.start);
    if (source->position() != wanted) {
        source->seekToSample(wanted);
    }
    auto remaining = static_cast<std::size_t>(to - from);
    float* cursor = out;
    while (remaining > 0) {
        const std::size_t got = source->read(cursor, remaining);
        if (got == 0) {
            break; // el medio se acaba antes que el clip: el resto, silencio
        }
        remaining -= got;
        cursor += got * kAudioChannels;
    }

    // Fundidos de los cortes.
    for (std::int64_t t = from; t < to; ++t) {
        float gain = 1.0f;
        if (segment.fadeIn && t - segment.start < kCutFade) {
            gain *= (static_cast<float>(t - segment.start) + 0.5f) / static_cast<float>(kCutFade);
        }
        if (segment.fadeOut && segment.end - t <= kCutFade) {
            gain *= (static_cast<float>(segment.end - t) - 0.5f) / static_cast<float>(kCutFade);
        }
        if (gain < 1.0f) {
            float* sample = out + static_cast<std::size_t>(t - from) * kAudioChannels;
            for (int c = 0; c < kAudioChannels; ++c) {
                sample[c] *= gain;
            }
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

    // Primer segmento que termina después de `start`.
    auto it = std::upper_bound(m_segments.begin(), m_segments.end(), start,
                               [](std::int64_t t, const Segment& s) { return t < s.end; });
    for (; it != m_segments.end() && it->start < end; ++it) {
        const std::int64_t from = std::max(start, it->start);
        const std::int64_t to = std::min(end, it->end);
        readSegment(*it, from, to, out + static_cast<std::size_t>(from - start) * kAudioChannels);
    }
    m_position = end;
    return n;
}

} // namespace sharkcut
