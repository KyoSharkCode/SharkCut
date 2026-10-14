#include "media/decode_worker.h"

#include <exception>
#include <utility>

namespace sharkcut {

DecodeWorker::DecodeWorker(std::string path, HwAccel hwAccel, Callbacks callbacks,
                           std::size_t capacity)
    : m_path(std::move(path)), m_hwAccel(hwAccel), m_callbacks(std::move(callbacks)),
      m_capacity(capacity == 0 ? 1 : capacity), m_thread([this] { run(); }) {}

DecodeWorker::~DecodeWorker() {
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    m_thread.join();
}

void DecodeWorker::seek(std::int64_t index) {
    {
        std::lock_guard lock(m_mutex);
        m_seekTarget = index < 0 ? 0 : index;
        m_queue.clear();
        m_endOfStream = false;
    }
    m_wake.notify_all();
}

std::shared_ptr<const VideoFrame> DecodeWorker::takeUpTo(std::int64_t index) {
    std::shared_ptr<const VideoFrame> result;
    {
        std::lock_guard lock(m_mutex);
        if (m_seekTarget >= 0) {
            return nullptr; // La cola pertenece a la posición anterior a la búsqueda.
        }
        while (!m_queue.empty() && m_queue.front()->index <= index) {
            result = std::move(m_queue.front());
            m_queue.pop_front();
        }
    }
    if (result) {
        m_wake.notify_all(); // Hay hueco en la cola: el hilo puede seguir.
    }
    return result;
}

bool DecodeWorker::hasFrame(std::int64_t index) const {
    std::lock_guard lock(m_mutex);
    return m_seekTarget < 0 && !m_queue.empty() && m_queue.front()->index <= index &&
           index <= m_queue.back()->index;
}

bool DecodeWorker::finished() const {
    std::lock_guard lock(m_mutex);
    return m_seekTarget < 0 && m_endOfStream && m_queue.empty();
}

std::string DecodeWorker::hwAccelName() const {
    std::lock_guard lock(m_mutex);
    return m_hwAccelName;
}

void DecodeWorker::run() {
    std::unique_ptr<VideoDecoder> decoder;
    try {
        decoder = std::make_unique<VideoDecoder>(m_path, m_hwAccel);
    } catch (const std::exception& e) {
        if (m_callbacks.failed) {
            m_callbacks.failed(e.what());
        }
        return;
    }
    if (m_callbacks.opened) {
        m_callbacks.opened(decoder->info());
    }

    for (;;) {
        std::int64_t seekTarget = -1;
        {
            std::unique_lock lock(m_mutex);
            m_wake.wait(lock, [this] {
                return m_stop || m_seekTarget >= 0 ||
                       (!m_endOfStream && m_queue.size() < m_capacity);
            });
            if (m_stop) {
                return;
            }
            seekTarget = std::exchange(m_seekTarget, -1);
        }

        try {
            if (seekTarget >= 0) {
                decoder->seekToFrame(seekTarget);
            }
            std::optional<VideoFrame> frame = decoder->nextFrame();

            std::lock_guard lock(m_mutex);
            if (m_seekTarget >= 0) {
                continue; // Llegó otra búsqueda mientras se decodificaba: se descarta.
            }
            m_hwAccelName = decoder->hwAccelName();
            if (frame) {
                m_queue.push_back(std::make_shared<const VideoFrame>(std::move(*frame)));
            } else {
                m_endOfStream = true;
            }
        } catch (const std::exception& e) {
            {
                std::lock_guard lock(m_mutex);
                m_endOfStream = true;
            }
            if (m_callbacks.failed) {
                m_callbacks.failed(e.what());
            }
        }
        if (m_callbacks.framesAvailable) {
            m_callbacks.framesAvailable();
        }
    }
}

} // namespace sharkcut
