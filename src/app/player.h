#pragma once

#include "core/time.h"
#include "media/decode_worker.h"
#include "media/video_frame.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <memory>

// Reproductor de un clip (fase 0): abre el archivo, mantiene el reloj de
// reproducción y decide qué fotograma se ve en cada momento.
//
// De momento solo video; en la etapa 3 el audio pasará a ser el reloj maestro.
class Player : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(qint64 frameCount READ frameCount NOTIFY loadedChanged)
    Q_PROPERTY(qint64 currentFrame READ currentFrame NOTIFY currentFrameChanged)
    Q_PROPERTY(QString timecode READ timecode NOTIFY currentFrameChanged)
    Q_PROPERTY(QString durationTimecode READ durationTimecode NOTIFY loadedChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY loadedChanged)
    Q_PROPERTY(QString videoDescription READ videoDescription NOTIFY loadedChanged)
    Q_PROPERTY(QString frameRateText READ frameRateText NOTIFY loadedChanged)
    Q_PROPERTY(bool variableFrameRate READ variableFrameRate NOTIFY loadedChanged)
    Q_PROPERTY(QString decoderText READ decoderText NOTIFY decoderChanged)
    Q_PROPERTY(bool hardwareDecoding READ hardwareDecoding NOTIFY decoderChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)

public:
    explicit Player(QObject* parent = nullptr);
    ~Player() override;

    bool loaded() const { return m_loaded; }
    bool busy() const { return m_busy; }
    bool playing() const { return m_playing; }
    qint64 frameCount() const { return m_frameCount; }
    qint64 currentFrame() const { return m_currentFrame; }
    QString timecode() const;
    QString durationTimecode() const;
    QString fileName() const { return m_fileName; }
    QString videoDescription() const { return m_videoDescription; }
    QString frameRateText() const { return m_frameRateText; }
    bool variableFrameRate() const { return m_variableFrameRate; }
    QString decoderText() const;
    bool hardwareDecoding() const { return !m_hwAccelName.isEmpty(); }
    QString errorString() const { return m_errorString; }

    // Fotograma que se debe mostrar ahora (lo lee el visor).
    std::shared_ptr<const sharkcut::VideoFrame> videoFrame() const { return m_frame; }

    Q_INVOKABLE void open(const QUrl& url);
    Q_INVOKABLE void openPath(const QString& path);
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void seek(qint64 frame);
    Q_INVOKABLE void step(int frames);

    // Avanza el reloj de reproducción; se llama una vez por refresco de pantalla.
    Q_INVOKABLE void advance();

signals:
    void loadedChanged();
    void busyChanged();
    void playingChanged();
    void currentFrameChanged();
    void decoderChanged();
    void errorStringChanged();
    void frameChanged(); // hay un fotograma nuevo para el visor

private:
    void onOpened(const sharkcut::VideoStreamInfo& info);
    void onFailed(const QString& message);
    void onFramesAvailable();
    void showFrame(std::shared_ptr<const sharkcut::VideoFrame> frame);
    void setCurrentFrame(qint64 frame);
    void setBusy(bool busy);
    void setPlaying(bool playing);
    void setError(const QString& message);
    void updateDecoder();

    std::unique_ptr<sharkcut::DecodeWorker> m_worker;
    quint64 m_session = 0; // descarta avisos de un archivo anterior

    bool m_loaded = false;
    bool m_busy = false;
    bool m_playing = false;
    qint64 m_frameCount = 0;
    qint64 m_currentFrame = 0;
    qint64 m_pendingFrame = -1; // fotograma pedido en pausa que aún no ha llegado
    sharkcut::FrameRate m_rate;
    QString m_fileName;
    QString m_videoDescription;
    QString m_frameRateText;
    bool m_variableFrameRate = false;
    QString m_hwAccelName;
    QString m_errorString;

    QElapsedTimer m_clock;
    qint64 m_clockStartFrame = 0;

    std::shared_ptr<const sharkcut::VideoFrame> m_frame;
};
