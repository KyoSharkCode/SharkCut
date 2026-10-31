#pragma once

#include <QAbstractListModel>
#include <QPointer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <vector>

class Editor;
namespace sharkcut {
struct Caption;
}

// Los clips de la secuencia activa que caen en la parte visible de la línea de tiempo
// (con un margen de media pantalla a cada lado), para la línea de tiempo.
//
// Con VODs troceados en miles de clips, crear un elemento de QML por clip (y volver a
// crearlos todos en cada edición) congelaba la interfaz. Aquí solo existen los que se
// ven, y al editar o desplazarse se actualizan las filas que cambian, sin rehacer el
// resto. Cada fila tiene un rol, "datosClip", con los datos de Editor::clipEntry().
//
// Con la vista muy alejada, los clips de menos de unos píxeles de ancho se agrupan con
// sus vecinos de la misma pista en un solo bloque ({block: true, count, track, start,
// length, video}), que se dibuja como una franja: así el número de elementos depende
// del ancho de la pantalla y no de cuántos cortes tiene el VOD.
class TimelineClipsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(Editor* editor READ editor WRITE setEditor NOTIFY editorChanged)
    // Tramo visible, en fotogramas de la secuencia.
    Q_PROPERTY(double firstFrame READ firstFrame WRITE setFirstFrame NOTIFY rangeChanged)
    Q_PROPERTY(double lastFrame READ lastFrame WRITE setLastFrame NOTIFY rangeChanged)
    // Escala de la vista (píxeles por fotograma), para agrupar los clips diminutos.
    Q_PROPERTY(
        double pixelsPerFrame READ pixelsPerFrame WRITE setPixelsPerFrame NOTIFY rangeChanged)
    // Subtítulos en vez de clips: filas {id, track (pista de subtítulos), start, length,
    // text} (o bloques {block: true, count, track, start, length}).
    Q_PROPERTY(bool captions READ captions WRITE setCaptions NOTIFY editorChanged)
    // Ids que siempre se cargan aunque estén fuera del tramo visible (lo que se arrastra:
    // si la vista se desplaza lejos, su elemento no se destruye a mitad del arrastre).
    Q_PROPERTY(QVariantList pinned READ pinned WRITE setPinned NOTIFY pinnedChanged)

public:
    explicit TimelineClipsModel(QObject* parent = nullptr);

    Editor* editor() const { return m_editor; }
    void setEditor(Editor* editor);
    double firstFrame() const { return m_first; }
    void setFirstFrame(double frame);
    double lastFrame() const { return m_last; }
    void setLastFrame(double frame);
    double pixelsPerFrame() const { return m_scale; }
    bool captions() const { return m_captions; }
    void setCaptions(bool captions);
    void setPixelsPerFrame(double scale);
    QVariantList pinned() const;
    void setPinned(const QVariantList& ids);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void editorChanged();
    void rangeChanged();
    void pinnedChanged();

private:
    // Rehace la ventana de clips cargados si lo visible se sale de ella (o siempre, con
    // `force`, tras una edición).
    void refresh(bool force);
    // Pide un refresh al volver al bucle de eventos: al hacer zoom cambian la escala y
    // el desplazamiento uno detrás de otro, y solo vale el estado final.
    void scheduleRefresh(bool force);

    struct Row {
        qint64 id = 0;
        QVariantMap clip;
    };
    std::vector<Row> collectClips() const;
    std::vector<Row> collectCaptions() const;
    static Row captionRow(std::size_t track, const sharkcut::Caption& caption);

    QPointer<Editor> m_editor;
    double m_first = 0.0;
    double m_last = 0.0;
    double m_scale = 1.0;
    std::int64_t m_windowStart = 0;
    std::int64_t m_windowEnd = -1;
    std::vector<Row> m_rows;
    bool m_captions = false;
    std::vector<qint64> m_pinned;
    bool isPinned(qint64 id) const;
    bool m_refreshPending = false;
    bool m_forcePending = false;
};
