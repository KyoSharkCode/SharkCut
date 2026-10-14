#pragma once

#include "compositionsource.h"
#include "core/time.h"
#include "media/decode_worker.h"
#include "media/video_frame.h"
#include "playbackclock.h"

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace sharkcut {
class AudioOutput;
class AudioWorker;
} // namespace sharkcut

// Reproductor de un clip (fase 0): abre el archivo, mantiene el reloj de
// reproducción y decide qué fotograma se ve en cada momento.
//
// Si el clip tiene audio, el reloj es el de la salida de audio (las muestras que ya
// han sonado) y el video lo sigue. Sin audio, o sin tarjeta de sonido, se usa un
// temporizador.
class Player : public CompositionSource {
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
    Q_PROPERTY(QString filePath READ filePath NOTIFY loadedChanged)
    // Marcas de entrada y salida (-1 = sin marca). La salida es el último fotograma incluido.
    Q_PROPERTY(qint64 markIn READ markIn NOTIFY marksChanged)
    Q_PROPERTY(qint64 markOut READ markOut NOTIFY marksChanged)
    Q_PROPERTY(QString markInTimecode READ markInTimecode NOTIFY marksChanged)
    Q_PROPERTY(QString markOutTimecode READ markOutTimecode NOTIFY marksChanged)
    Q_PROPERTY(QString markedDurationTimecode READ markedDurationTimecode NOTIFY marksChanged)
    Q_PROPERTY(QString videoDescription READ videoDescription NOTIFY loadedChanged)
    Q_PROPERTY(QString frameRateText READ frameRateText NOTIFY loadedChanged)
    Q_PROPERTY(bool variableFrameRate READ variableFrameRate NOTIFY loadedChanged)
    Q_PROPERTY(QString decoderText READ decoderText NOTIFY decoderChanged)
    Q_PROPERTY(bool hardwareDecoding READ hardwareDecoding NOTIFY decoderChanged)
    Q_PROPERTY(bool hasAudio READ hasAudio NOTIFY audioChanged)
    Q_PROPERTY(QString audioDescription READ audioDescription NOTIFY audioChanged)
    Q_PROPERTY(QString audioOutputText READ audioOutputText CONSTANT)
    Q_PROPERTY(bool audioOutputAvailable READ audioOutputAvailable CONSTANT)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
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
    QString filePath() const { return m_filePath; }
    qint64 markIn() const { return m_markIn; }
    qint64 markOut() const { return m_markOut; }
    // Tramo marcado efectivo [in, out] (sin marcas: el clip entero).
    qint64 rangeIn() const { return m_markIn >= 0 ? m_markIn : 0; }
    qint64 rangeOut() const { return m_markOut >= 0 ? m_markOut : m_frameCount - 1; }
    QString markInTimecode() const;
    QString markOutTimecode() const;
    QString markedDurationTimecode() const;
    sharkcut::FrameRate frameRate() const { return m_rate; }
    QString videoDescription() const { return m_videoDescription; }
    QString frameRateText() const { return m_frameRateText; }
    bool variableFrameRate() const { return m_variableFrameRate; }
    QString decoderText() const;
    bool hardwareDecoding() const { return !m_hwAccelName.isEmpty(); }
    bool hasAudio() const { return m_hasAudio; }
    QString audioDescription() const;
    QString audioOutputText() const { return m_audioOutputText; }
    bool audioOutputAvailable() const { return m_audioOutput != nullptr; }
    bool muted() const { return m_muted; }
    void setMuted(bool muted);
    QString errorString() const { return m_errorString; }

    sharkcut::CompositorInput composition() const override;
    QSize canvasSize() const override;

    Q_INVOKABLE void open(const QUrl& url);
    Q_INVOKABLE void openPath(const QString& path);
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void seek(qint64 frame);
    Q_INVOKABLE void step(int frames);
    Q_INVOKABLE void setMarkIn();  // en el fotograma actual (tecla I)
    Q_INVOKABLE void setMarkOut(); // en el fotograma actual (tecla O)
    Q_INVOKABLE void clearMarks();

    // Avanza la reproducción según el reloj; se llama una vez por refresco de pantalla.
    Q_INVOKABLE void advance();

signals:
    void loadedChanged();
    void busyChanged();
    void playingChanged();
    void currentFrameChanged();
    void decoderChanged();
    void audioChanged();
    void mutedChanged();
    void errorStringChanged();
    void marksChanged();

private:
    void onOpened(const sharkcut::VideoStreamInfo& info);
    void onFailed(const QString& message);
    void onFramesAvailable();
    void onAudioOpened(const sharkcut::AudioStreamInfo& info);
    void showFrame(std::shared_ptr<const sharkcut::VideoFrame> frame);
    void setCurrentFrame(qint64 frame);
    void setBusy(bool busy);
    void setPlaying(bool playing);
    void setError(const QString& message);
    void updateDecoder();

    std::unique_ptr<sharkcut::AudioOutput> m_audioOutput; // nullptr si no hay salida
    QString m_audioOutputText;

    std::unique_ptr<sharkcut::DecodeWorker> m_worker;
    std::unique_ptr<sharkcut::AudioWorker> m_audioWorker;
    quint64 m_session = 0; // descarta avisos de un archivo anterior

    bool m_loaded = false;
    bool m_busy = false;
    bool m_playing = false;
    bool m_muted = false;
    qint64 m_frameCount = 0;
    qint64 m_currentFrame = 0;
    qint64 m_pendingFrame = -1; // fotograma pedido en pausa que aún no ha llegado
    sharkcut::FrameRate m_rate;
    QString m_fileName;
    QString m_filePath;
    qint64 m_markIn = -1;
    qint64 m_markOut = -1;
    QString m_videoDescription;
    QString m_frameRateText;
    bool m_variableFrameRate = false;
    QString m_hwAccelName;
    bool m_hasAudio = false;
    sharkcut::AudioStreamInfo m_audioInfo;
    QString m_errorString;

    PlaybackClock m_clock;

    std::shared_ptr<const sharkcut::VideoFrame> m_frame;
};
