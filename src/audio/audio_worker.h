#pragma once

#include "media/media_info.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace sharkcut {

class AudioOutput;

// Decodifica el audio de un clip en un hilo propio y mantiene lleno el búfer de la
// salida de audio. Las rellamadas se invocan desde ese hilo.
class AudioWorker {
public:
    struct Callbacks {
        std::function<void(const AudioStreamInfo&)> opened;
        std::function<void(const std::string& message)> failed; // incluye "sin audio"
    };

    AudioWorker(std::string path, AudioOutput& output, Callbacks callbacks);
    ~AudioWorker();

    AudioWorker(const AudioWorker&) = delete;
    AudioWorker& operator=(const AudioWorker&) = delete;

    // Empieza a sonar desde `sample` cuando haya suficiente audio preparado.
    // Devuelve el identificador de esta petición (ver AudioOutput::Clock::requestId).
    std::uint64_t play(std::int64_t sample);

    void pause();

    // true si se llegó al final del audio y ya no queda nada por sonar.
    bool finished() const;

private:
    void run();

    const std::string m_path;
    AudioOutput& m_output;
    const Callbacks m_callbacks;

    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stop = false;
    bool m_wantPlaying = false;
    std::int64_t m_requestSample = -1; // petición pendiente (-1 = ninguna)
    std::uint64_t m_requestId = 0;
    std::atomic<bool> m_endOfStream{false};

    std::thread m_thread; // último miembro: arranca cuando todo lo demás existe
};

} // namespace sharkcut
