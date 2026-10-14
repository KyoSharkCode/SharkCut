#pragma once

#include <QColor>
#include <QPointer>
#include <QQuickRhiItem>
#include <QtQml/qqmlregistration.h>

class Player;

// Visor de video dibujado en la GPU con QRhi (Direct3D 11 en Windows).
// Muestra el fotograma actual del Player ajustado a su relación de aspecto.
class VideoViewer : public QQuickRhiItem {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(Player* player READ player WRITE setPlayer NOTIFY playerChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY
                   backgroundColorChanged)

public:
    explicit VideoViewer(QQuickItem* parent = nullptr);

    Player* player() const { return m_player; }
    void setPlayer(Player* player);

    QColor backgroundColor() const { return m_backgroundColor; }
    void setBackgroundColor(const QColor& color);

signals:
    void playerChanged();
    void backgroundColorChanged();

protected:
    QQuickRhiItemRenderer* createRenderer() override;

private:
    QPointer<Player> m_player;
    QColor m_backgroundColor = Qt::black;
};
