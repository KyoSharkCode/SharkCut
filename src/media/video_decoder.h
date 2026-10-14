#pragma once

#include "media/media_info.h"
#include "media/video_frame.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace sharkcut {

enum class HwAccel {
    Auto, // Usa la GPU si es posible (D3D11VA/NVDEC en Windows); si no, la CPU.
    None, // Solo CPU (resultados reproducibles, p. ej. en tests).
};

// Decodifica el flujo de video de un archivo, fotograma a fotograma y en orden de
// presentación. Los fotogramas se entregan en NV12 en memoria.
//
// No es seguro usar una misma instancia desde varios hilos a la vez.
class VideoDecoder {
public:
    // Abre `path` (UTF-8). Lanza MediaError si no se puede abrir o no tiene video.
    explicit VideoDecoder(const std::string& path, HwAccel hwAccel = HwAccel::Auto);
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
    void seekToFrame(std::int64_t index);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace sharkcut
