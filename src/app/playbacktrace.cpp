#include "playbacktrace.h"

#include <algorithm>

namespace {

// Tras empezar o saltar, lo que tarda en llegar el primer fotograma es normal.
constexpr qint64 kGraceMs = 300;
// Un clip que se ve sin fotograma durante este tiempo: no aparece.
constexpr qint64 kMissingMs = 100;
// Un clip con el mismo fotograma durante este tiempo, cuando ya tocaba otro: congelado.
constexpr qint64 kFrozenMs = 150;
// Se da por puesto al día cuando no va más de estos fotogramas del medio por detrás.
constexpr qint64 kCaughtUpFrames = 2;
// Tramas de audio por milisegundo (48 kHz).
constexpr double kAudioFramesPerMs = 48.0;
// Rachas de audio más cortas no se apuntan (p. ej. el último bloque, incompleto, al
// llegar al final de la secuencia): no se notan.
constexpr qint64 kMinAudioGapFrames = 20 * 48;

} // namespace

void PlaybackTrace::begin(qint64 frame, std::function<QString(qint64)> timecode) {
    m_timecode = std::move(timecode);
    m_active = true;
    m_clock.start();
    m_graceUntilMs = kGraceMs;
    m_firstFrame = frame;
    m_lastFrame = frame;
    m_clips.clear();
    m_played = 0;
    m_lateFrames = 0;
    m_events = 0;
    m_audioMissingTotal = 0;
    m_audioBurst = 0;
    m_audioBursts = 0;
}

void PlaybackTrace::restart(qint64 frame) {
    if (!m_active) {
        return;
    }
    for (auto& [key, state] : m_clips) {
        close(key, state, "al saltar");
    }
    m_clips.clear();
    m_graceUntilMs = m_clock.elapsed() + kGraceMs;
    m_lastFrame = frame;
    m_audioBurst = 0;
}

void PlaybackTrace::close(const Key& key, ClipState& state, const char* how) {
    if (state.open == Problem::None) {
        return;
    }
    qInfo("Visor: pista %zu, clip %llu: %s tras %lld fotogramas sin llegar a tiempo (%lld ms)",
          key.first + 1, static_cast<unsigned long long>(key.second), how,
          static_cast<long long>(state.lateFrames),
          static_cast<long long>(m_clock.elapsed() - state.openedMs));
    state.open = Problem::None;
}

void PlaybackTrace::step(qint64 frame, const std::vector<Visible>& visible,
                         const Describe& describe) {
    if (!m_active) {
        return;
    }
    const qint64 now = m_clock.elapsed();
    const qint64 advanced = std::max<qint64>(0, frame - m_lastFrame);
    m_lastFrame = frame;
    m_played += advanced;
    const bool grace = now < m_graceUntilMs;

    std::map<Key, ClipState> next;
    for (const Visible& v : visible) {
        const Key key{v.track, v.clip};
        ClipState state;
        if (const auto it = m_clips.find(key); it != m_clips.end()) {
            state = it->second;
            m_clips.erase(it);
        } else {
            state.firstSeenMs = now;
            state.firstSeenFrame = frame;
            state.changedMs = now;
            state.changedFrame = frame;
        }
        if (v.shown != state.shown) {
            state.shown = v.shown;
            state.changedMs = now;
            state.changedFrame = frame;
        }
        Problem problem = Problem::None;
        if (v.shown < 0) {
            if (!v.finished && now - state.firstSeenMs >= kMissingMs) {
                problem = Problem::Missing;
            }
        } else if (!v.finished && v.shown < v.wanted && now - state.changedMs >= kFrozenMs) {
            problem = Problem::Frozen;
        }
        const bool caughtUp = v.finished || (v.shown >= 0 && v.wanted - v.shown <= kCaughtUpFrames);

        if (state.open != Problem::None) {
            if (caughtUp) {
                close(key, state, state.open == Problem::Missing ? "aparece" : "se pone al día");
            } else {
                state.lateFrames += advanced;
                m_lateFrames += advanced;
            }
        } else if (problem != Problem::None && !grace) {
            state.open = problem;
            state.openedMs = now;
            state.openedFrame = frame;
            // Desde que dejó de llegar (el umbral para darlo por perdido también cuenta).
            state.lateFrames =
                frame - (problem == Problem::Missing ? state.firstSeenFrame : state.changedFrame);
            m_lateFrames += state.lateFrames;
            ++m_events;
            qInfo("Visor: pista %zu, clip %llu %s en %s: %s", v.track + 1,
                  static_cast<unsigned long long>(v.clip),
                  problem == Problem::Missing ? "sin fotograma" : "congelado",
                  qUtf8Printable(m_timecode ? m_timecode(frame) : QString::number(frame)),
                  qUtf8Printable(describe ? describe(v) : QString()));
        }
        next.emplace(key, state);
    }
    // Los que ya no se ven.
    for (auto& [key, state] : m_clips) {
        close(key, state, "deja de verse");
    }
    m_clips = std::move(next);
}

void PlaybackTrace::audioMissing(qint64 frames) {
    if (!m_active) {
        return;
    }
    if (frames > 0 && m_clock.elapsed() >= m_graceUntilMs) {
        if (m_audioBurst == 0) {
            m_audioBurstFrame = m_lastFrame;
        }
        m_audioBurst += frames;
        return;
    }
    if (frames == 0 && m_audioBurst > 0) {
        if (m_audioBurst < kMinAudioGapFrames) {
            m_audioBurst = 0;
            return;
        }
        ++m_audioBursts;
        m_audioMissingTotal += m_audioBurst;
        qInfo("Audio: sin muestras preparadas durante %.0f ms en %s (el reloj del visor se "
              "paró)",
              static_cast<double>(m_audioBurst) / kAudioFramesPerMs,
              qUtf8Printable(m_timecode ? m_timecode(m_audioBurstFrame)
                                        : QString::number(m_audioBurstFrame)));
        m_audioBurst = 0;
    }
}

void PlaybackTrace::end() {
    if (!m_active) {
        return;
    }
    audioMissing(0); // la racha que quedara abierta
    for (auto& [key, state] : m_clips) {
        close(key, state, "sigue atrasado al pausar");
    }
    m_clips.clear();
    m_active = false;
    const qint64 played = m_played;
    if (played <= 0) {
        return;
    }
    const QString from = m_timecode ? m_timecode(m_firstFrame) : QString::number(m_firstFrame);
    const QString to = m_timecode ? m_timecode(m_lastFrame) : QString::number(m_lastFrame);
    qInfo("Reproducción: de %s a %s (%lld fotogramas vistos, %lld ms); no llegaron a tiempo %lld "
          "fotogramas en %d sucesos; audio sin muestras %.0f ms en %d rachas",
          qUtf8Printable(from), qUtf8Printable(to), static_cast<long long>(played),
          static_cast<long long>(m_clock.elapsed()), static_cast<long long>(m_lateFrames), m_events,
          static_cast<double>(m_audioMissingTotal) / kAudioFramesPerMs, m_audioBursts);
}
