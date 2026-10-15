#pragma once

#include "render/compositor.h"

#include <QObject>
#include <QSize>

// Algo que el visor puede mostrar: hoy, la secuencia activa del editor (Editor).
class CompositionSource : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    // Qué dibujar ahora. Lo lee el hilo de render mientras la interfaz espera.
    virtual sharkcut::CompositorInput composition() const = 0;

    // Resolución del lienzo (proyecto).
    virtual QSize canvasSize() const = 0;

signals:
    void compositionChanged(); // hay algo nuevo que dibujar
};
