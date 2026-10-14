#include "media/sequence_audio.h"

#include "media/audio_decoder.h"
#include "media/media_error.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <numbers>
#include <vector>

namespace sharkcut {

namespace {

std::unique_ptr<AudioDecoder> openOptional(const std::string& path) {
    try {
        return std::make_unique<AudioDecoder>(path);
    } catch (const std::exception&) {
        return nullptr; // clip sin audio: aportará silencio
    }
}

} // namespace

SequenceAudioSource::SequenceAudioSource(const SequenceLayout& layout, FrameRate rate,
                                         const std::string& pathA, const std::string& pathB)
    : m_layout(layout), m_rate(rate), m_a(openOptional(pathA)), m_b(openOptional(pathB)) {
    if (!m_a && !m_b) {
        throw MediaError("Ninguno de los dos clips tiene audio");
    }
    m_info = m_a ? m_a->info() : m_b->info();

    m_aEnd = samplesOf(layout.transitionEnd());
    m_bStart = samplesOf(layout.transitionStart());
    m_total = samplesOf(layout.length());
    m_aFirst = samplesOf(layout.a.in);
    m_bFirst = samplesOf(layout.b.in);
    seekToSample(0);
}

SequenceAudioSource::~SequenceAudioSource() = default;

const AudioStreamInfo& SequenceAudioSource::info() const {
    return m_info;
}

std::int64_t SequenceAudioSource::samplesOf(std::int64_t frames) const {
    return rescale(frames, m_rate.frameDuration(), kAudioSampleDuration);
}

void SequenceAudioSource::seekToSample(std::int64_t sample) {
    m_position = std::clamp<std::int64_t>(sample, 0, m_total);
    if (m_a) {
        m_a->seekToSample(m_aFirst + std::min(m_position, m_aEnd));
    }
    if (m_b) {
        m_b->seekToSample(m_bFirst + std::max<std::int64_t>(m_position - m_bStart, 0));
    }
}

void SequenceAudioSource::readClip(AudioDecoder* decoder, float* out, std::size_t frames) {
    std::size_t got = decoder ? decoder->read(out, frames) : 0;
    // Clip sin audio o que se acaba antes de tiempo: silencio.
    std::memset(out + got * kAudioChannels, 0, (frames - got) * kAudioChannels * sizeof(float));
}

std::size_t SequenceAudioSource::read(float* out, std::size_t frames) {
    const auto n = static_cast<std::size_t>(
        std::clamp<std::int64_t>(m_total - m_position, 0, static_cast<std::int64_t>(frames)));
    if (n == 0) {
        return 0;
    }
    const std::int64_t start = m_position;
    const std::int64_t end = start + static_cast<std::int64_t>(n);
    std::memset(out, 0, n * kAudioChannels * sizeof(float));

    // Tramo de A: [0, m_aEnd).  Tramo de B: [m_bStart, m_total).
    std::vector<float> a;
    if (start < m_aEnd) {
        a.resize(static_cast<std::size_t>(std::min(end, m_aEnd) - start) * kAudioChannels);
        readClip(m_a.get(), a.data(), a.size() / kAudioChannels);
    }
    std::vector<float> b;
    const std::int64_t bFrom = std::max(start, m_bStart);
    if (end > m_bStart) {
        b.resize(static_cast<std::size_t>(end - bFrom) * kAudioChannels);
        readClip(m_b.get(), b.data(), b.size() / kAudioChannels);
    }

    const double overlap = static_cast<double>(std::max<std::int64_t>(m_aEnd - m_bStart, 1));
    for (std::int64_t t = start; t < end; ++t) {
        double gainA = 1.0;
        double gainB = 1.0;
        if (t >= m_bStart && t < m_aEnd) {
            // Fundido cruzado de potencia constante: cos² + sin² = 1.
            const double x = (static_cast<double>(t - m_bStart) + 0.5) / overlap;
            gainA = std::cos(x * std::numbers::pi / 2.0);
            gainB = std::sin(x * std::numbers::pi / 2.0);
        }
        const auto o = static_cast<std::size_t>(t - start) * kAudioChannels;
        for (int c = 0; c < kAudioChannels; ++c) {
            float value = 0.0f;
            if (t < m_aEnd) {
                value += static_cast<float>(gainA) * a[o + static_cast<std::size_t>(c)];
            }
            if (t >= m_bStart) {
                const auto bo = static_cast<std::size_t>(t - bFrom) * kAudioChannels;
                value += static_cast<float>(gainB) * b[bo + static_cast<std::size_t>(c)];
            }
            out[o + static_cast<std::size_t>(c)] = value;
        }
    }
    m_position = end;
    return n;
}

} // namespace sharkcut
