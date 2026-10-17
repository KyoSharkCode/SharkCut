#include "thumbnails.h"

#include <QGuiApplication>
#include <QNetworkProxyFactory>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>

int main(int argc, char* argv[]) {
#ifdef Q_OS_WIN
    // En Windows el motor de render usa Direct3D 11: es la API que comparte
    // texturas con NVDEC (decodificación) y NVENC (exportación).
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
#endif

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("SharkCut"));
    QGuiApplication::setApplicationVersion(QStringLiteral(SHARKCUT_VERSION));
    QGuiApplication::setOrganizationName(QStringLiteral("KyoSumiVT"));
    // La descarga del modelo de subtítulos usa el proxy del sistema, si lo hay.
    QNetworkProxyFactory::setUseSystemConfiguration(true);

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
    engine.addImageProvider(QStringLiteral("miniaturas"), new ThumbnailProvider);
    engine.addImageProvider(QStringLiteral("fotogramas"), new FrameThumbnailProvider);
    engine.loadFromModule("SharkCut", "Main");

    return QGuiApplication::exec();
}
