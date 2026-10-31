#pragma once

#include "media/waveform.h"

#include <QColor>
#include <QObject>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <deque>
#include <map>
#include <memory>

class Editor;

// Formas de onda de los medios del proyecto, calculadas en segundo plano.
//
// Se guardan en memoria y, si el proyecto ya tiene archivo, en su caché
// (<proyecto>.sharkp.cache/ondas/), nunca junto a los medios originales. Al volver a
// abrir el proyecto se leen de ahí sin decodificar otra vez el audio.
class WaveformCache : public QObject {
    Q_OBJECT

public:
    explicit WaveformCache(QObject* parent = nullptr);
    ~WaveformCache() override; // abandona los cálculos pendientes y espera

    // La forma de onda del archivo (de su pista de audio `audioStream`; −1 = la
    // principal), o nullptr mientras se calcula (al terminar llega `ready`) o si no tiene
    // audio.
    std::shared_ptr<const sharkcut::Waveform> get(const QString& mediaPath, int audioStream = -1);

    // La pide de antemano (al importar o abrir el proyecto), la de la pista principal: se
    // calculan de una en una y solo mientras no se calcula ninguna que un clip espera.
    void prefetch(const QString& mediaPath);

    // Carpeta de la caché en disco del proyecto activo ("" = solo en memoria).
    void setDirectory(const QString& directory);

    // Guarda en `directory` las ondas ya calculadas de esos medios (al guardar por
    // primera vez un proyecto, lo calculado mientras no tenía archivo).
    void saveTo(const QString& directory, const QStringList& mediaPaths);

    // true mientras se lee de la caché o se calcula.
    bool pending(const QString& mediaPath, int audioStream = -1) const;

signals:
    void ready(const QString& mediaPath);

private:
    struct Entry {
        std::shared_ptr<const sharkcut::Waveform> waveform;
        bool pending = false;
        bool failed = false;
        bool queued = false;     // en la cola de las preparadas de antemano
        bool background = false; // calculándose de antemano
    };
    using Key = std::pair<QString, int>; // archivo y pista de audio
    void start(const Key& key, bool background);
    void pumpPrefetch();
    void finish(const Key& key, std::shared_ptr<const sharkcut::Waveform> waveform);
    void save(const QString& directory, const Key& key,
              std::shared_ptr<const sharkcut::Waveform> waveform);

    QThreadPool m_pool;
    std::map<Key, Entry> m_entries;
    std::deque<Key> m_prefetch; // las que esperan su turno, en orden
    bool m_prefetching = false;
    int m_foreground = 0; // las que se calculan porque un clip las espera
    QString m_directory;
    std::shared_ptr<std::atomic<bool>> m_cancel = std::make_shared<std::atomic<bool>>(false);
};

// Dibuja la onda de un clip de audio en la línea de tiempo: solo la parte visible
// (el elemento ocupa lo que se ve del clip, no el clip entero).
class WaveformView : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(Editor* editor READ editor WRITE setEditor NOTIFY changed)
    Q_PROPERTY(qint64 mediaId READ mediaId WRITE setMediaId NOTIFY changed)
    // Pista de audio del archivo (Clip::audioStream; −1 = la principal).
    Q_PROPERTY(int audioStream READ audioStream WRITE setAudioStream NOTIFY changed)
    // Fotograma del medio (en fotogramas del proyecto) que cae en x = 0.
    Q_PROPERTY(double firstFrame READ firstFrame WRITE setFirstFrame NOTIFY changed)
    Q_PROPERTY(double framesPerPixel READ framesPerPixel WRITE setFramesPerPixel NOTIFY changed)
    Q_PROPERTY(double fps READ fps WRITE setFps NOTIFY changed)
    Q_PROPERTY(double gainDb READ gainDb WRITE setGainDb NOTIFY changed)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY changed)
    // true mientras la onda se calcula (la interfaz muestra un aviso).
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)

public:
    explicit WaveformView(QQuickItem* parent = nullptr);

    Editor* editor() const { return m_editor; }
    void setEditor(Editor* editor);
    qint64 mediaId() const { return m_mediaId; }
    void setMediaId(qint64 id);
    int audioStream() const { return m_audioStream; }
    void setAudioStream(int stream);
    double firstFrame() const { return m_firstFrame; }
    void setFirstFrame(double frame);
    double framesPerPixel() const { return m_framesPerPixel; }
    void setFramesPerPixel(double value);
    double fps() const { return m_fps; }
    void setFps(double fps);
    double gainDb() const { return m_gainDb; }
    void setGainDb(double db);
    QColor color() const { return m_color; }
    void setColor(const QColor& color);

    bool loading() const { return m_loading; }

    void paint(QPainter* painter) override;

signals:
    void changed();
    void loadingChanged();

private:
    template <typename T> void set(T& member, const T& value);
    // Pide la onda en el hilo de la interfaz; paint() (hilo de render) solo la lee.
    void refresh();

    QPointer<Editor> m_editor;
    std::shared_ptr<const sharkcut::Waveform> m_waveform;
    bool m_loading = false;
    qint64 m_mediaId = 0;
    int m_audioStream = -1;
    double m_firstFrame = 0.0;
    double m_framesPerPixel = 1.0;
    double m_fps = 60.0;
    double m_gainDb = 0.0;
    QColor m_color = Qt::white;
};
