#pragma once

#include "media/media_info.h"
#include "media/video_frame.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sharkcut {

enum class HwAccel {
    Auto, // Usa la GPU si es posible (D3D11VA/NVDEC en Windows); si no, la CPU.
    None, // Solo CPU (resultados reproducibles, p. ej. en tests).
};

// Aceleración por hardware que usaría la decodificación en este equipo (p. ej.
// "d3d11va"), o cadena vacía si solo se puede por software. Crea el dispositivo para
// comprobarlo: no llamar desde el hilo de interfaz.
std::string probeHwDecodeDevice();

// Decodifica el flujo de video de un archivo, fotograma a fotograma y en orden de
// presentación. Los fotogramas se entregan en NV12 en memoria.
//
// No es seguro usar una misma instancia desde varios hilos a la vez.
class VideoDecoder {
public:
    // Abre `path` (UTF-8). Lanza MediaError si no se puede abrir o no tiene video.
    // `downscale` > 1: los fotogramas se entregan a 1/downscale de su tamaño (la vista
    // previa a menos resolución; las imágenes fijas y los colores sólidos, enteros).
    explicit VideoDecoder(const std::string& path, HwAccel hwAccel = HwAccel::Auto,
                          int downscale = 1);
    ~VideoDecoder();

    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    const VideoStreamInfo& info() const;

    // Nombre de la aceleración por hardware usada en el último fotograma
    // (p. ej. "d3d11va"), o cadena vacía si se decodificó por software.
    std::string hwAccelName() const;

    // Siguiente fotograma, o std::nullopt al llegar al final del clip.
    std::optional<VideoFrame> nextFrame();

    // Coloca el decodificador para que nextFrame() devuelva exactamente el fotograma
    // `index` (limitado al rango del clip). Si el clip termina antes de lo que indica
    // su duración, nextFrame() devuelve el último fotograma disponible.
    //
    // Si el destino está poco por delante de lo último entregado, sigue decodificando
    // en vez de volver al fotograma clave (arrastrar el cabezal hacia delante).
    //
    // `keepBefore`: además, guarda los `keepBefore` fotogramas anteriores al destino que
    // se decodifican de camino (hay que pasar por ellos de todos modos); se recogen con
    // takeKeptFrames() tras el siguiente nextFrame(). Sirve para ir hacia atrás (J) sin
    // volver al fotograma clave en cada paso.
    void seekToFrame(std::int64_t index, std::int64_t keepBefore = 0);

    // Como seekToFrame, pero nextFrame() devuelve el primer fotograma tras el salto (el
    // fotograma clave anterior a `index`) sin decodificar hasta `index`: mucho más rápido
    // y suficiente para miniaturas (puede quedarse unos segundos antes).
    void seekNear(std::int64_t index);

    // Los fotogramas guardados por seekToFrame(…, keepBefore), en orden.
    std::vector<VideoFrame> takeKeptFrames();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace sharkcut
