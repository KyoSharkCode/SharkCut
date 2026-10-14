#pragma once

#include "compositionsource.h"
#include "core/sequence.h"
#include "core/time.h"
#include "core/transition.h"
#include "media/decode_worker.h"
#include "playbackclock.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <memory>

class Player;

namespace sharkcut {
class AudioOutput;
class AudioWorker;
} // namespace sharkcut

// Laboratorio de transiciones (fase 1): reproduce la secuencia
// clip A → transición → clip B para ajustar la transición en directo.
//
// Cada clip es un tramo marcado (entrada/salida) de un video, tomado del reproductor.
class TransitionLab : public CompositionSource {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool hasClipA READ hasClipA NOTIFY clipsChanged)
    Q_PROPERTY(bool hasClipB READ hasClipB NOTIFY clipsChanged)
    Q_PROPERTY(QString clipAName READ clipAName NOTIFY clipsChanged)
    Q_PROPERTY(QString clipBName READ clipBName NOTIFY clipsChanged)
    Q_PROPERTY(QString clipARange READ clipARange NOTIFY clipsChanged)
    Q_PROPERTY(QString clipBRange READ clipBRange NOTIFY clipsChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY clipsChanged)

    Q_PROPERTY(QStringList transitionNames READ transitionNames CONSTANT)
    Q_PROPERTY(QStringList easingNames READ easingNames CONSTANT)
    Q_PROPERTY(
        int transitionIndex READ transitionIndex WRITE setTransitionIndex NOTIFY paramsChanged)
    Q_PROPERTY(int easingIndex READ easingIndex WRITE setEasingIndex NOTIFY paramsChanged)
    Q_PROPERTY(int durationFrames READ durationFrames WRITE setDurationFrames NOTIFY paramsChanged)
    Q_PROPERTY(double intensity READ intensity WRITE setIntensity NOTIFY paramsChanged)
    Q_PROPERTY(double blur READ blur WRITE setBlur NOTIFY paramsChanged)
    Q_PROPERTY(QString durationText READ durationText NOTIFY paramsChanged)

    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(qint64 frameCount READ frameCount NOTIFY layoutChanged)
    Q_PROPERTY(qint64 transitionStart READ transitionStart NOTIFY layoutChanged)
    Q_PROPERTY(qint64 transitionEnd READ transitionEnd NOTIFY layoutChanged)
    Q_PROPERTY(qint64 currentFrame READ currentFrame NOTIFY currentFrameChanged)
    Q_PROPERTY(QString timecode READ timecode NOTIFY currentFrameChanged)
    Q_PROPERTY(QString durationTimecode READ durationTimecode NOTIFY layoutChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)

public:
    explicit TransitionLab(QObject* parent = nullptr);
    ~TransitionLab() override;

    bool hasClipA() const { return !m_a.path.isEmpty(); }
    bool hasClipB() const { return !m_b.path.isEmpty(); }
    QString clipAName() const { return m_a.name; }
    QString clipBName() const { return m_b.name; }
    QString clipARange() const;
    QString clipBRange() const;
    bool ready() const { return hasClipA() && hasClipB() && m_layout.length() > 0; }

    QStringList transitionNames() const;
    QStringList easingNames() const;
    int transitionIndex() const;
    void setTransitionIndex(int index);
    int easingIndex() const;
    void setEasingIndex(int index);
    int durationFrames() const { return static_cast<int>(m_params.durationFrames); }
    void setDurationFrames(int frames);
    double intensity() const { return m_params.intensity; }
    void setIntensity(double value);
    double blur() const { return m_params.blur; }
    void setBlur(double value);
    QString durationText() const;

    bool playing() const { return m_playing; }
    bool busy() const { return m_busy; }
    qint64 frameCount() const { return m_layout.length(); }
    qint64 transitionStart() const { return m_layout.transitionStart(); }
    qint64 transitionEnd() const { return m_layout.transitionEnd(); }
    qint64 currentFrame() const { return m_currentFrame; }
    QString timecode() const;
    QString durationTimecode() const;
    bool muted() const { return m_muted; }
    void setMuted(bool muted);
    QString errorString() const { return m_errorString; }

    sharkcut::CompositorInput composition() const override;
    QSize canvasSize() const override { return m_canvasSize; }

    // Toma el tramo marcado del reproductor como clip A o B.
    Q_INVOKABLE void setClipA(Player* source);
    Q_INVOKABLE void setClipB(Player* source);
    Q_INVOKABLE void swapClips();
    Q_INVOKABLE void resetParams(); // valores por defecto de la transición elegida

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void seek(qint64 frame);
    Q_INVOKABLE void step(int frames);
    // Reproduce desde un segundo antes de la transición hasta un segundo después.
    Q_INVOKABLE void playTransition();

    // Avanza la reproducción según el reloj; se llama una vez por refresco de pantalla.
    Q_INVOKABLE void advance();

signals:
    void clipsChanged();
    void paramsChanged();
    void layoutChanged();
    void playingChanged();
    void busyChanged();
    void currentFrameChanged();
    void mutedChanged();
    void errorStringChanged();

private:
    struct Clip {
        QString path;
        QString name;
        sharkcut::ClipRange range;
        sharkcut::FrameRate rate;
        QSize size;
    };

    void setClip(Clip& clip, Player* source);
    void rebuild(); // nuevos clips o duración: recrear hilos y recolocar
    void restartAudio();
    void onFramesAvailable();
    void requestFrames(qint64 frame); // colocar los hilos de video para mostrar `frame`
    bool takeFrames(qint64 frame);    // recoger los fotogramas de `frame`; true si están
    void setCurrentFrame(qint64 frame);
    void setPlaying(bool playing);
    void setBusy(bool busy);
    void setError(const QString& message);
    QString rangeText(const Clip& clip) const;

    Clip m_a;
    Clip m_b;
    sharkcut::SequenceLayout m_layout;
    sharkcut::TransitionParams m_params = sharkcut::kTransitions[4].defaults; // Zoom In
    sharkcut::FrameRate m_rate{60, 1};
    QSize m_canvasSize{1920, 1080};

    std::unique_ptr<sharkcut::DecodeWorker> m_workerA;
    std::unique_ptr<sharkcut::DecodeWorker> m_workerB;
    std::unique_ptr<sharkcut::AudioOutput> m_audioOutput; // propia: no comparte búfer
    std::unique_ptr<sharkcut::AudioWorker> m_audioWorker;
    PlaybackClock m_clock;
    quint64 m_session = 0;

    std::shared_ptr<const sharkcut::VideoFrame> m_frameA;
    std::shared_ptr<const sharkcut::VideoFrame> m_frameB;
    qint64 m_pendingFrame = -1; // fotograma pedido en pausa que aún no ha llegado
    qint64 m_stopAt = -1;       // playTransition(): detenerse al llegar aquí
    qint64 m_currentFrame = 0;
    bool m_playing = false;
    bool m_busy = false;
    bool m_muted = false;
    QString m_errorString;
};
