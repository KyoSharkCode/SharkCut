#pragma once

#include "media/media_info.h"
#include "media/video_decoder.h"
#include "media/video_frame.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace sharkcut {

// Decodifica un clip en un hilo propio y mantiene una pequeña cola de fotogramas
// preparados por adelantado. Así la interfaz nunca espera a disco ni a la GPU.
//
// Guarda además los últimos fotogramas que pasaron por él (unos 64 MB): volver a uno de
// ellos (un paso atrás, J, arrastrar el cabezal hacia la izquierda) no decodifica nada.
// Al ir hacia atrás, cada búsqueda aprovecha para guardar los fotogramas anteriores al
// destino, así los pasos siguientes también salen de la caché.
//
// Las funciones de rellamada se invocan desde el hilo de decodificación.
class DecodeWorker {
public:
    struct Callbacks {
        std::function<void(const VideoStreamInfo&)> opened;
        std::function<void(const std::string& message)> failed;
        std::function<void()> framesAvailable;
    };

    DecodeWorker(std::string path, HwAccel hwAccel, Callbacks callbacks, std::size_t capacity = 8);
    ~DecodeWorker();

    DecodeWorker(const DecodeWorker&) = delete;
    DecodeWorker& operator=(const DecodeWorker&) = delete;

    // Vacía la cola y continúa decodificando a partir del fotograma `index`.
    void seek(std::int64_t index);

    // Saca de la cola los fotogramas con índice <= `index` y devuelve el último de
    // ellos (el que corresponde mostrar), o nullptr si aún no hay ninguno.
    std::shared_ptr<const VideoFrame> takeUpTo(std::int64_t index);

    // true si el fotograma `index` está en la cola (entre el primero y el último).
    bool hasFrame(std::int64_t index) const;

    // true si se llegó al final del clip y ya no quedan fotogramas en la cola.
    bool finished() const;

    // Aceleración por hardware del último fotograma ("" = por software).
    std::string hwAccelName() const;

private:
    void run();

    const std::string m_path;
    const HwAccel m_hwAccel;
    const Callbacks m_callbacks;
    const std::size_t m_capacity;

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<std::shared_ptr<const VideoFrame>> m_queue;
    std::int64_t m_seekTarget = -1; // búsqueda pendiente (-1 = ninguna)
    std::int64_t m_keepBefore = 0;  // fotogramas a guardar antes del destino (hacia atrás)
    // Tras servir una búsqueda desde la caché, dónde seguir decodificando cuando se acabe
    // lo servido (-1 = nada pendiente).
    std::int64_t m_resumeAt = -1;
    std::int64_t m_lastRequest = -1; // último destino pedido (para notar que se va atrás)
    std::map<std::int64_t, std::shared_ptr<const VideoFrame>> m_cache;
    std::size_t m_cacheBytes = 0;

    // Guarda en la caché (con el mutex tomado) y quita lo más lejano a lo último pedido.
    void remember(const std::shared_ptr<const VideoFrame>& frame);
    bool m_endOfStream = false;
    bool m_stop = false;
    std::string m_hwAccelName;

    std::thread m_thread; // último miembro: arranca cuando todo lo demás existe
};

} // namespace sharkcut
