#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <vector>

namespace sharkcut {

// Búfer circular de floats para un solo productor y un solo consumidor, sin bloqueos:
// el hilo de decodificación escribe y el callback de audio (tiempo real) lee.
//
// reset() solo es seguro cuando el consumidor no está leyendo (ver AudioOutput).
class AudioRingBuffer {
public:
    explicit AudioRingBuffer(std::size_t capacity) : m_data(capacity + 1) {}

    std::size_t capacity() const { return m_data.size() - 1; }

    std::size_t available() const {
        const std::size_t w = m_write.load(std::memory_order_acquire);
        const std::size_t r = m_read.load(std::memory_order_acquire);
        return w >= r ? w - r : m_data.size() - r + w;
    }

    std::size_t freeSpace() const { return capacity() - available(); }

    // Escribe hasta `count` floats; devuelve cuántos cupieron. (Productor.)
    std::size_t write(const float* src, std::size_t count) {
        const std::size_t w = m_write.load(std::memory_order_relaxed);
        const std::size_t r = m_read.load(std::memory_order_acquire);
        const std::size_t free = (r > w ? r - w : m_data.size() - w + r) - 1;
        count = std::min(count, free);
        const std::size_t first = std::min(count, m_data.size() - w);
        std::memcpy(m_data.data() + w, src, first * sizeof(float));
        std::memcpy(m_data.data(), src + first, (count - first) * sizeof(float));
        m_write.store((w + count) % m_data.size(), std::memory_order_release);
        return count;
    }

    // Lee hasta `count` floats; devuelve cuántos había. (Consumidor.)
    std::size_t read(float* dst, std::size_t count) {
        const std::size_t r = m_read.load(std::memory_order_relaxed);
        const std::size_t w = m_write.load(std::memory_order_acquire);
        const std::size_t avail = w >= r ? w - r : m_data.size() - r + w;
        count = std::min(count, avail);
        const std::size_t first = std::min(count, m_data.size() - r);
        std::memcpy(dst, m_data.data() + r, first * sizeof(float));
        std::memcpy(dst + first, m_data.data(), (count - first) * sizeof(float));
        m_read.store((r + count) % m_data.size(), std::memory_order_release);
        return count;
    }

    // Vacía el búfer. Requiere que nadie esté leyendo ni escribiendo a la vez.
    void reset() {
        m_read.store(0, std::memory_order_release);
        m_write.store(0, std::memory_order_release);
    }

private:
    std::vector<float> m_data; // una posición libre distingue "lleno" de "vacío"
    std::atomic<std::size_t> m_read{0};
    std::atomic<std::size_t> m_write{0};
};

} // namespace sharkcut
