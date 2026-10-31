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

    // `downscale`: como en VideoDecoder (la vista previa a menos resolución).
    DecodeWorker(std::string path, HwAccel hwAccel, Callbacks callbacks, std::size_t capacity = 8,
                 int downscale = 1);
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

    // true si, sin buscar nada, lo siguiente que entregará es el fotograma `index` + 1: está
    // en la cola o es el que se está decodificando ahora (tras entregar `index`). Sirve
    // para pasar el hilo a la mitad nueva de un corte sin buscar (sin tirón).
    bool continuesAfter(std::int64_t index) const;

    // true si se llegó al final del clip y ya no quedan fotogramas en la cola.
    bool finished() const;

    // Aceleración por hardware del último fotograma ("" = por software).
    std::string hwAccelName() const;

    // Estado del hilo para el registro (cuando un fotograma no llega a tiempo).
    struct Status {
        bool opened = false;           // ya abrió el archivo
        std::int64_t seekPending = -1; // búsqueda pedida que aún no empezó (-1 = ninguna)
        std::int64_t firstQueued = -1; // primer y último fotograma de la cola (-1 = vacía)
        std::int64_t lastQueued = -1;
        std::int64_t nextIndex = -1; // el siguiente que pondrá en la cola
        bool endOfStream = false;
        bool waiting = false;   // parado esperando (cola llena, al final o tras la caché)
        std::size_t cached = 0; // fotogramas en la caché (para ir hacia atrás)
        std::string hwAccel;    // "" = por software
    };
    Status status() const;

private:
    void run();

    const std::string m_path;
    const HwAccel m_hwAccel;
    const int m_downscale;
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
    // Tras servir desde la caché yendo hacia delante, colocarse ya en `m_resumeAt` (sin
    // esperar a que se consuma lo servido): la colocación es lo lento y, si espera, la
    // imagen se queda quieta al acabarse lo servido. Hacia atrás (J) no: cada paso es otra
    // búsqueda y se perdería el trabajo.
    bool m_resumeEarly = false;
    std::int64_t m_lastRequest = -1; // último destino pedido (para notar que se va atrás)
    std::int64_t m_nextIndex = -1;   // el siguiente que se pondrá en la cola
    std::map<std::int64_t, std::shared_ptr<const VideoFrame>> m_cache;
    std::size_t m_cacheBytes = 0;

    // Guarda en la caché (con el mutex tomado) y quita lo más lejano a lo último pedido.
    void remember(const std::shared_ptr<const VideoFrame>& frame);
    bool m_endOfStream = false;
    bool m_opened = false;
    bool m_waiting = false; // el hilo está parado en m_wake
    bool m_stop = false;
    std::string m_hwAccelName;

    std::thread m_thread; // último miembro: arranca cuando todo lo demás existe
};

} // namespace sharkcut
