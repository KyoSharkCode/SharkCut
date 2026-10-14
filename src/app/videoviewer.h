#pragma once

#include <QColor>
#include <QPointer>
#include <QQuickRhiItem>
#include <QtQml/qqmlregistration.h>

class CompositionSource;

// Visor dibujado en la GPU con QRhi (Direct3D 11 en Windows). Muestra lo que
// compone el motor (sharkcut::Compositor) para la fuente indicada.
class VideoViewer : public QQuickRhiItem {
    Q_OBJECT
    QML_ELEMENT

    // Player o TransitionLab (cualquier CompositionSource).
    Q_PROPERTY(QObject* source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY
                   backgroundColorChanged)

public:
    explicit VideoViewer(QQuickItem* parent = nullptr);

    QObject* source() const;
    void setSource(QObject* source);
    CompositionSource* compositionSource() const { return m_source; }

    QColor backgroundColor() const { return m_backgroundColor; }
    void setBackgroundColor(const QColor& color);

signals:
    void sourceChanged();
    void backgroundColorChanged();

protected:
    QQuickRhiItemRenderer* createRenderer() override;

private:
    QPointer<CompositionSource> m_source;
    QColor m_backgroundColor = Qt::black;
};
