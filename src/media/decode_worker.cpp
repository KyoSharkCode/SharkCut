#include "media/decode_worker.h"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iterator>
#include <utility>

namespace sharkcut {

namespace {

// Memoria para la caché de cada hilo (≈ 20 fotogramas 1080p, 5 en 4K).
constexpr std::size_t kCacheBytes = 64u << 20;
// Al ir hacia atrás, cuántos fotogramas antes del destino se guardan de camino.
constexpr std::int64_t kReverseKeep = 20;

std::size_t bytesOf(const VideoFrame& frame) {
    return frame.y.size() + frame.uv.size();
}

} // namespace

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
    index = std::max<std::int64_t>(index, 0);
    bool served = false;
    {
        std::lock_guard lock(m_mutex);
        const std::int64_t previous = std::exchange(m_lastRequest, index);
        m_queue.clear();
        m_endOfStream = false;
        auto it = m_cache.find(index);
        if (it != m_cache.end()) {
            // Desde la caché: el destino y los que le siguen sin hueco.
            std::int64_t next = index;
            for (; it != m_cache.end() && it->first == next; ++it, ++next) {
                m_queue.push_back(it->second);
                if (m_queue.size() >= m_capacity) {
                    ++next;
                    break;
                }
            }
            m_seekTarget = -1;
            m_resumeAt = next;
            served = true;
        } else {
            // Un paso (o pocos) hacia atrás: guardar también los anteriores.
            const bool backwards = previous > index && previous - index <= 8;
            m_seekTarget = index;
            m_keepBefore = backwards ? kReverseKeep : 0;
            m_resumeAt = -1;
        }
    }
    m_wake.notify_all();
    if (served && m_callbacks.framesAvailable) {
        m_callbacks.framesAvailable();
    }
}

void DecodeWorker::remember(const std::shared_ptr<const VideoFrame>& frame) {
    const auto [it, inserted] = m_cache.emplace(frame->index, frame);
    if (!inserted) {
        return;
    }
    m_cacheBytes += bytesOf(*frame);
    while (m_cacheBytes > kCacheBytes && m_cache.size() > 1) {
        // Se olvida lo más lejano a lo último pedido.
        const auto first = m_cache.begin();
        const auto last = std::prev(m_cache.end());
        const auto victim =
            std::llabs(first->first - m_lastRequest) >= std::llabs(last->first - m_lastRequest)
                ? first
                : last;
        m_cacheBytes -= bytesOf(*victim->second);
        m_cache.erase(victim);
    }
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
        std::int64_t keepBefore = 0;
        {
            std::unique_lock lock(m_mutex);
            m_wake.wait(lock, [this] {
                // Tras servir desde la caché, se espera a que se consuma lo servido.
                const bool wantsMore = m_resumeAt >= 0
                                           ? m_queue.empty()
                                           : !m_endOfStream && m_queue.size() < m_capacity;
                return m_stop || m_seekTarget >= 0 || wantsMore;
            });
            if (m_stop) {
                return;
            }
            seekTarget = std::exchange(m_seekTarget, -1);
            keepBefore = std::exchange(m_keepBefore, 0);
            if (seekTarget < 0 && m_resumeAt >= 0) {
                seekTarget = std::exchange(m_resumeAt, -1); // seguir tras lo servido
            }
        }

        try {
            if (seekTarget >= 0) {
                decoder->seekToFrame(seekTarget, keepBefore);
            }
            std::optional<VideoFrame> frame = decoder->nextFrame();
            std::vector<VideoFrame> kept = decoder->takeKeptFrames();

            std::lock_guard lock(m_mutex);
            for (VideoFrame& k : kept) {
                remember(std::make_shared<const VideoFrame>(std::move(k)));
            }
            if (m_seekTarget >= 0 || m_resumeAt >= 0) {
                continue; // Llegó otra búsqueda mientras se decodificaba: se descarta.
            }
            m_hwAccelName = decoder->hwAccelName();
            if (frame) {
                auto shared = std::make_shared<const VideoFrame>(std::move(*frame));
                remember(shared);
                m_queue.push_back(std::move(shared));
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
