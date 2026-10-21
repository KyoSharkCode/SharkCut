#include "logging.h"
#include "thumbnails.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QNetworkProxyFactory>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>

namespace {

// Un clic fuera del campo de texto que tiene el teclado (un valor de Propiedades, el
// buscador…) se lo quita: si no, ese campo se quedaba con Espacio, C, V, Supr… y los
// atajos dejaban de funcionar hasta volver a hacer clic en otro campo.
class FocusRelease : public QObject {
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() != QEvent::MouseButtonPress) {
            return false;
        }
        auto* window = qobject_cast<QQuickWindow*>(watched);
        QQuickItem* focused = window ? window->activeFocusItem() : nullptr;
        if (!focused ||
            !(focused->inherits("QQuickTextInput") || focused->inherits("QQuickTextEdit"))) {
            return false;
        }
        const QPointF point =
            focused->mapFromScene(static_cast<QMouseEvent*>(event)->scenePosition());
        if (!focused->contains(point)) {
            focused->setFocus(false, Qt::MouseFocusReason);
        }
        return false; // el clic sigue su camino
    }
};

} // namespace

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
    logging::install();
    // La descarga del modelo de subtítulos usa el proxy del sistema, si lo hay.
    QNetworkProxyFactory::setUseSystemConfiguration(true);

    FocusRelease focusRelease;
    app.installEventFilter(&focusRelease);

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
    engine.addImageProvider(QStringLiteral("miniaturas"), new ThumbnailProvider);
    engine.addImageProvider(QStringLiteral("fotogramas"), new FrameThumbnailProvider);
    engine.loadFromModule("SharkCut", "Main");

    const int result = QGuiApplication::exec();
    logging::closing();
    return result;
}
