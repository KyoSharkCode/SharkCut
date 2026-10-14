#include "playbackclock.h"

#include "audio/audio_output.h"
#include "audio/audio_worker.h"
#include "core/time.h"
#include "media/audio_decoder.h"

#include <exception>

void PlaybackClock::setAudio(sharkcut::AudioOutput* output, sharkcut::AudioWorker* worker) {
    if (m_worker && m_worker != worker) {
        m_worker->pause();
    }
    m_output = output;
    m_worker = worker;
    if (m_running) {
        start(nowUs()); // el audio cambió a mitad: seguir desde donde íbamos
    }
}

void PlaybackClock::start(qint64 anchorUs) {
    m_anchorUs = anchorUs;
    m_timer.start();
    m_running = true;
    m_audioClock = m_output && m_worker;
    if (m_audioClock) {
        const std::int64_t sample =
            sharkcut::rescale(anchorUs, sharkcut::kMicroseconds, sharkcut::kAudioSampleDuration);
        m_audioRequest = m_worker->play(sample);
    }
}

void PlaybackClock::stop() {
    if (m_worker) {
        m_worker->pause();
    }
    m_running = false;
    m_audioClock = false;
}

qint64 PlaybackClock::nowUs() {
    if (!m_running) {
        return m_anchorUs;
    }
    if (m_audioClock) {
        const sharkcut::AudioOutput::Clock clock = m_output->clock();
        if (clock.requestId != m_audioRequest) {
            // El audio aún se está preparando: el reloj espera en el punto de partida.
            m_timer.start();
            return m_anchorUs;
        }
        m_anchorUs = sharkcut::rescale(clock.sample, sharkcut::kAudioSampleDuration,
                                       sharkcut::kMicroseconds);
        m_timer.start();
        if (!m_worker->finished()) {
            return m_anchorUs;
        }
        // El audio terminó antes que el video: se sigue con el temporizador.
        m_audioClock = false;
    }
    return m_anchorUs + m_timer.nsecsElapsed() / 1000;
}

std::unique_ptr<sharkcut::AudioOutput> createAudioOutput(QString* description) {
    try {
        const bool useNull = qEnvironmentVariable("SHARKCUT_AUDIO_DEVICE") == QLatin1String("null");
        auto output = std::make_unique<sharkcut::AudioOutput>(
            useNull ? sharkcut::AudioOutput::Device::Null : sharkcut::AudioOutput::Device::Default);
        if (description) {
            *description =
                QStringLiteral("%1 · %2").arg(QString::fromStdString(output->backendName()),
                                              QString::fromStdString(output->deviceName()));
        }
        return output;
    } catch (const std::exception& e) {
        // Sin tarjeta de sonido se reproduce igual, en silencio y con temporizador.
        if (description) {
            *description = QStringLiteral("No disponible (%1)").arg(QString::fromUtf8(e.what()));
        }
        return nullptr;
    }
}
