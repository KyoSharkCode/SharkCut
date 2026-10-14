#include "audio/audio_worker.h"

#include "audio/audio_output.h"
#include "media/audio_decoder.h"

#include <chrono>
#include <exception>
#include <memory>
#include <utility>
#include <vector>

namespace sharkcut {

namespace {
constexpr std::size_t kChunkFrames = 1024;
// Audio mínimo preparado antes de empezar a sonar (100 ms).
constexpr std::size_t kPrebufferFloats = kAudioSampleRate / 10 * kAudioChannels;
} // namespace

AudioWorker::AudioWorker(SourceFactory makeSource, AudioOutput& output, Callbacks callbacks)
    : m_makeSource(std::move(makeSource)), m_output(output), m_callbacks(std::move(callbacks)),
      m_thread([this] { run(); }) {}

AudioWorker::~AudioWorker() {
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    m_thread.join();
    m_output.setPlaying(false);
}

std::uint64_t AudioWorker::play(std::int64_t sample) {
    std::uint64_t id = 0;
    {
        // Con el cerrojo tomado, como cuando el hilo decide empezar a sonar.
        std::lock_guard lock(m_mutex);
        m_wantPlaying = true;
        m_requestSample = sample < 0 ? 0 : sample;
        id = ++m_requestId;
        m_output.setPlaying(false); // silencio hasta que el audio nuevo esté listo
    }
    m_wake.notify_all();
    return id;
}

void AudioWorker::pause() {
    std::lock_guard lock(m_mutex);
    m_wantPlaying = false;
    m_output.setPlaying(false);
}

bool AudioWorker::finished() const {
    return m_endOfStream.load(std::memory_order_acquire) && m_output.buffer().available() == 0;
}

void AudioWorker::run() {
    std::unique_ptr<AudioSource> decoder;
    try {
        decoder = m_makeSource();
    } catch (const std::exception& e) {
        if (m_callbacks.failed) {
            m_callbacks.failed(e.what());
        }
        return;
    }
    if (m_callbacks.opened) {
        m_callbacks.opened(decoder->info());
    }

    std::vector<float> chunk(kChunkFrames * kAudioChannels);
    bool active = false;       // hay una reproducción en curso que alimentar
    bool prebuffering = false; // esperando a tener audio suficiente para sonar
    std::uint64_t activeId = 0;

    for (;;) {
        std::int64_t request = -1;
        {
            std::unique_lock lock(m_mutex);
            // El callback no avisa al liberar espacio: se revisa cada pocos milisegundos.
            m_wake.wait_for(lock, std::chrono::milliseconds(5),
                            [this] { return m_stop || m_requestSample >= 0; });
            if (m_stop) {
                return;
            }
            if (m_requestSample >= 0) {
                request = std::exchange(m_requestSample, -1);
                activeId = m_requestId;
            }
        }

        try {
            if (request >= 0) {
                // Primero se limpia el fin de audio y después se anuncia la nueva petición:
                // así nadie ve la petición nueva con el "fin" de la anterior.
                m_endOfStream.store(false, std::memory_order_release);
                m_output.reset(request, activeId);
                decoder->seekToSample(request);
                active = true;
                prebuffering = true;
            }
            if (!active) {
                continue;
            }

            // Rellenar el búfer mientras haya hueco.
            AudioRingBuffer& buffer = m_output.buffer();
            while (!m_endOfStream.load(std::memory_order_relaxed) &&
                   buffer.freeSpace() >= chunk.size()) {
                const std::size_t frames = decoder->read(chunk.data(), kChunkFrames);
                if (frames == 0) {
                    m_endOfStream.store(true, std::memory_order_release);
                    break;
                }
                buffer.write(chunk.data(), frames * kAudioChannels);
            }

            if (prebuffering && (buffer.available() >= kPrebufferFloats ||
                                 m_endOfStream.load(std::memory_order_relaxed))) {
                prebuffering = false;
                std::lock_guard lock(m_mutex);
                // Solo si nadie pausó ni pidió otra posición mientras tanto.
                if (m_wantPlaying && m_requestSample < 0 && activeId == m_requestId) {
                    m_output.setPlaying(true);
                }
            }
        } catch (const std::exception& e) {
            active = false;
            m_endOfStream.store(true, std::memory_order_release);
            if (m_callbacks.failed) {
                m_callbacks.failed(e.what());
            }
        }
    }
}

} // namespace sharkcut
