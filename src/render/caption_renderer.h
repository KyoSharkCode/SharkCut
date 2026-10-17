#pragma once

// Dibujo de subtítulos: el texto con su estilo (fuente, contorno, sombra y caja) en una
// imagen que el compositor pone encima del video. Lo usan el visor y la exportación.

#include "core/project.h"
#include "render/compositor.h"

#include <QImage>
#include <QRectF>
#include <QSize>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace sharkcut {

// Escala de las medidas del estilo (pensadas para 1080 de lado corto) en un lienzo.
double captionScale(QSize canvas);

// Dibuja el subtítulo para un lienzo de `canvas`. La imagen (RGBA premultiplicado) va en
// `rect` (píxeles del lienzo); vacía si no hay texto.
CompositorOverlay renderCaption(const std::string& text, const CaptionStyle& style, QSize canvas);

// Subtítulos ya dibujados, para no repetirlo en cada fotograma. Cada dueño (visor,
// exportación) tiene la suya: no es segura entre hilos.
class CaptionImageCache {
public:
    // Los subtítulos visibles en `frame`, de S1 hacia arriba.
    std::vector<CompositorOverlay> overlaysAt(const Project& project, std::int64_t frame,
                                              QSize canvas);
    // Uno concreto (también para la vista previa de un estilo que aún no se aplicó).
    CompositorOverlay get(const std::string& text, const CaptionStyle& style, QSize canvas);

private:
    std::unordered_map<std::string, CompositorOverlay> m_items;
    std::vector<std::string> m_order; // los más antiguos primero
};

} // namespace sharkcut
