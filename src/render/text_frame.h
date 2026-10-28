#pragma once

// Textos libres de la línea de tiempo (TextClip): se dibujan con el motor de los
// subtítulos y se convierten en un fotograma del tamaño del lienzo (NV12 con su plano de
// transparencia), con el texto en el centro. El compositor lo trata como un clip de video
// más, así la colocación, la animación y los efectos funcionan igual (y el visor y la
// exportación salen iguales).

#include "core/project.h"
#include "media/video_frame.h"
#include "render/caption_renderer.h"

#include <QRectF>
#include <QSize>

#include <memory>
#include <utility>
#include <vector>

namespace sharkcut {

// El fotograma de la imagen de un texto (RGBA premultiplicado) colocada en `rect` de un
// lienzo de `canvas`. Y'CbCr de BT.709 en rango completo; el color de cada bloque de 2×2
// se promedia según la transparencia de sus píxeles (sin bordes oscuros).
VideoFrame textImageToFrame(const QImage& image, QRectF rect, QSize canvas);

// Textos ya convertidos, para no repetirlo en cada fotograma. Cada dueño (visor,
// exportación) tiene la suya: no es segura entre hilos.
class TextFrameCache {
public:
    struct Entry {
        std::shared_ptr<const VideoFrame> frame; // nullptr si no hay texto
        QRectF rect;                             // dónde está el texto, en píxeles del lienzo
    };
    Entry get(const TextClip& text, QSize canvas);

private:
    CaptionImageCache m_images;
    std::vector<std::pair<std::shared_ptr<const QImage>, Entry>> m_frames; // los nuevos al final
};

} // namespace sharkcut
