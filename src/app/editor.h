#pragma once

#include "compositionsource.h"
#include "core/history.h"
#include "core/project.h"
#include "media/decode_worker.h"
#include "playbackclock.h"
#include "player.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace sharkcut {
class AudioOutput;
class AudioWorker;
} // namespace sharkcut

// Editor (fase 2): el proyecto con su línea de tiempo, las herramientas de edición
// y la reproducción de la línea de tiempo en el visor.
//
// Toda modificación del proyecto pasa por el historial (deshacer/rehacer).
// Reproducción: dos hilos de video, uno para el clip que se ve y otro preparado en
// el primer fotograma del clip siguiente, así los cortes no se atascan. El audio de
// la línea de tiempo marca el reloj.
class Editor : public CompositionSource {
    Q_OBJECT
    QML_ELEMENT

    // Proyecto
    Q_PROPERTY(QVariantList mediaItems READ mediaItems NOTIFY projectChanged)
    Q_PROPERTY(QVariantList clips READ clips NOTIFY projectChanged)
    Q_PROPERTY(qint64 frameCount READ frameCount NOTIFY projectChanged)
    Q_PROPERTY(double fps READ fps NOTIFY projectChanged)
    Q_PROPERTY(QString projectDescription READ projectDescription NOTIFY projectChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY projectChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY projectChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY projectChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY projectChanged)
    Q_PROPERTY(bool importing READ importing NOTIFY importingChanged)
    // Selección (ids de clips; un clip enlazado se considera seleccionado con su pareja)
    Q_PROPERTY(QVariantList selection READ selection NOTIFY selectionChanged)
    Q_PROPERTY(QString selectionDescription READ selectionDescription NOTIFY selectionChanged)
    // Reproducción
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(qint64 currentFrame READ currentFrame NOTIFY currentFrameChanged)
    Q_PROPERTY(QString timecode READ timecode NOTIFY currentFrameChanged)
    Q_PROPERTY(QString durationTimecode READ durationTimecode NOTIFY projectChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)

public:
    explicit Editor(QObject* parent = nullptr);
    ~Editor() override;

    const sharkcut::Project& project() const { return m_history.project(); }

    QVariantList mediaItems() const;
    QVariantList clips() const;
    qint64 frameCount() const { return project().length(); }
    double fps() const;
    QString projectDescription() const;
    bool canUndo() const { return m_history.canUndo(); }
    bool canRedo() const { return m_history.canRedo(); }
    QString undoText() const;
    QString redoText() const;
    bool importing() const { return m_importing > 0; }
    QVariantList selection() const;
    QString selectionDescription() const;

    bool playing() const { return m_playing; }
    bool busy() const { return m_busy; }
    qint64 currentFrame() const { return m_currentFrame; }
    QString timecode() const;
    QString durationTimecode() const;
    bool muted() const { return m_muted; }
    void setMuted(bool muted);
    QString errorString() const { return m_errorString; }

    sharkcut::CompositorInput composition() const override;
    QSize canvasSize() const override;

    // --- Biblioteca ---
    // Analiza los archivos en segundo plano y los añade a la biblioteca. Con
    // `placeAt` >= 0 también los coloca, uno tras otro, desde ese fotograma.
    Q_INVOKABLE void importFiles(const QList<QUrl>& urls, qint64 placeAt = -1);
    Q_INVOKABLE void importPaths(const QStringList& paths, qint64 placeAt = -1);
    Q_INVOKABLE void removeMedia(qint64 mediaId);
    Q_INVOKABLE QString mediaPath(qint64 mediaId) const;

    // --- Edición ---
    // Coloca el medio entero en `frame` (sobrescribiendo); `frame` < 0: al final.
    Q_INVOKABLE void addMedia(qint64 mediaId, qint64 frame = -1);
    // Coloca el tramo marcado del reproductor de origen: al final o en el cabezal.
    Q_INVOKABLE void addFromSource(Player* source, bool atPlayhead);
    Q_INVOKABLE void splitAtPlayhead();                      // divide lo que hay bajo el cabezal
    Q_INVOKABLE void splitClip(qint64 clipId, qint64 frame); // cuchilla
    Q_INVOKABLE void removeSelection();
    Q_INVOKABLE void trimStart(qint64 clipId, qint64 frame);
    Q_INVOKABLE void trimEnd(qint64 clipId, qint64 frame);
    Q_INVOKABLE qint64 limitTrimStart(qint64 clipId, qint64 frame) const;
    Q_INVOKABLE qint64 limitTrimEnd(qint64 clipId, qint64 frame) const;
    // Mueve el clip (o toda la selección, si el clip está en ella).
    Q_INVOKABLE void moveClips(qint64 clipId, qint64 delta);
    Q_INVOKABLE qint64 limitMove(qint64 clipId, qint64 delta) const;
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    Q_INVOKABLE void select(qint64 clipId, bool add);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool isSelected(qint64 clipId) const;

    // Código de tiempo de un fotograma de la línea de tiempo (para la regla).
    Q_INVOKABLE QString timecodeFor(qint64 frame) const;

    // --- Reproducción ---
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void seek(qint64 frame);
    Q_INVOKABLE void step(int frames);
    // Avanza la reproducción según el reloj; se llama una vez por refresco de pantalla.
    Q_INVOKABLE void advance();

signals:
    void projectChanged();
    void selectionChanged();
    void importingChanged();
    void playingChanged();
    void busyChanged();
    void currentFrameChanged();
    void mutedChanged();
    void errorStringChanged();

private:
    // Un hilo de decodificación colocado para un clip concreto.
    struct VideoSlot {
        sharkcut::ObjectId media = 0;
        std::unique_ptr<sharkcut::DecodeWorker> worker;
        sharkcut::ObjectId clip = 0; // clip para el que está colocado
        std::int64_t seekIndex = -1; // fotograma del medio al que se colocó
    };

    // Aplica una edición registrándola en el historial.
    bool edit(const QString& label, const std::function<bool(sharkcut::Project&)>& change);
    void onProjectChanged(); // tras editar, deshacer o rehacer
    void restartAudio();
    void resetVideoSlots();

    std::int64_t sourceIndex(const sharkcut::Clip& clip, std::int64_t frame) const;
    int slotFor(const sharkcut::Clip& clip, int avoid);
    void placeSlot(int slot, const sharkcut::Clip& clip, std::int64_t frame, bool force);
    const sharkcut::Clip* nextVideoClip(std::int64_t frame) const;
    void prepareNext(std::int64_t frame);
    void requestFrames(qint64 frame); // colocar los hilos para mostrar `frame`
    bool takeFrames(qint64 frame);    // recoger el fotograma de `frame`; true si está
    void onFramesAvailable();

    void setCurrentFrame(qint64 frame);
    void setPlaying(bool playing);
    void setBusy(bool busy);
    void setError(const QString& message);
    void finishImport(std::vector<sharkcut::MediaItem> items, QStringList errors, qint64 placeAt);

    sharkcut::History m_history;
    std::vector<sharkcut::ObjectId> m_selection;
    int m_importing = 0;

    std::array<VideoSlot, 2> m_slots;
    int m_current = -1; // hueco con el clip que se ve
    std::shared_ptr<const sharkcut::VideoFrame> m_frame;
    sharkcut::ObjectId m_frameClip = 0; // clip al que pertenece m_frame

    std::unique_ptr<sharkcut::AudioOutput> m_audioOutput;
    std::unique_ptr<sharkcut::AudioWorker> m_audioWorker;
    PlaybackClock m_clock;

    qint64 m_currentFrame = 0;
    qint64 m_pendingFrame = -1; // fotograma pedido en pausa que aún no ha llegado
    bool m_playing = false;
    bool m_busy = false;
    bool m_muted = false;
    QString m_errorString;
};
