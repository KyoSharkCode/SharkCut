#pragma once

#include <QQuickAsyncImageProvider>

// Miniaturas de los medios para la biblioteca ("image://miniaturas/<ruta codificada>").
//
// Se decodifica un fotograma (al 10 % del video) en el grupo de hilos de Qt, nunca en
// el de la interfaz, y se guarda en memoria para no repetirlo.
class ThumbnailProvider : public QQuickAsyncImageProvider {
public:
    QQuickImageResponse* requestImageResponse(const QString& id,
                                              const QSize& requestedSize) override;
};

// Fotogramas de los clips de video en la línea de tiempo
// ("image://fotogramas/<ruta codificada>@<microsegundos>"): pequeños (160 px) y en una
// caché propia, más grande, porque una línea de tiempo pide muchos.
class FrameThumbnailProvider : public QQuickAsyncImageProvider {
public:
    QQuickImageResponse* requestImageResponse(const QString& id,
                                              const QSize& requestedSize) override;
};
