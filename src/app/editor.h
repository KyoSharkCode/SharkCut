#pragma once

#include "compositionsource.h"
#include "core/edit.h"
#include "core/history.h"
#include "core/project.h"
#include "media/decode_worker.h"
#include "playbackclock.h"
#include "player.h"
#include "waveforms.h"

#include <QElapsedTimer>
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

class QOffscreenSurface;

namespace sharkcut {
class AudioOutput;
class AudioWorker;
class SequenceExporter;
} // namespace sharkcut

// Editor: el proyecto con su línea de tiempo, las herramientas de edición y la
// reproducción de la línea de tiempo en el visor.
//
// Toda modificación del proyecto pasa por el historial (deshacer/rehacer).
// Reproducción: por cada pista de video visible, dos hilos de decodificación, uno para
// el clip que se ve y otro preparado en el primer fotograma del clip siguiente, así los
// cortes no se atascan. La mezcla de audio de la línea de tiempo marca el reloj.
class Editor : public CompositionSource {
    Q_OBJECT
    QML_ELEMENT

    // Proyecto
    Q_PROPERTY(QVariantList mediaItems READ mediaItems NOTIFY projectChanged)
    Q_PROPERTY(QVariantList clips READ clips NOTIFY projectChanged)
    // Pistas en el orden de `tracks` del proyecto: {index, name, kind, number, hidden,
    // muted, locked}
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY projectChanged)
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
    // El clip seleccionado (su parte de video y la de audio), para Propiedades y el visor:
    // vacío si no hay uno solo. Ver selectedClip().
    Q_PROPERTY(QVariantMap selectedClip READ selectedClip NOTIFY selectedClipChanged)
    // Archivo del proyecto
    Q_PROPERTY(QString projectName READ projectName NOTIFY fileChanged)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY fileChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
    Q_PROPERTY(bool fileBusy READ fileBusy NOTIFY fileBusyChanged)
    // Lienzo del proyecto: "16:9", "9:16", "1:1"… y si lo fija el primer clip
    Q_PROPERTY(QString canvasLabel READ canvasLabel NOTIFY projectChanged)
    Q_PROPERTY(QSize canvas READ canvasSize NOTIFY projectChanged)
    Q_PROPERTY(QUrl defaultExportFolder READ defaultExportFolder NOTIFY fileChanged)
    // Medidor de la mezcla: nivel de cada canal en dB (−60 = silencio, 0 = máximo)
    Q_PROPERTY(double meterLeft READ meterLeft NOTIFY metersChanged)
    Q_PROPERTY(double meterRight READ meterRight NOTIFY metersChanged)
    // Exportación
    Q_PROPERTY(bool exporting READ exporting NOTIFY exportChanged)
    Q_PROPERTY(double exportProgress READ exportProgress NOTIFY exportChanged)
    Q_PROPERTY(QString exportMessage READ exportMessage NOTIFY exportChanged)
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
    QVariantList tracks() const;
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
    QVariantMap selectedClip() const;

    // Forma de onda de un medio (nullptr mientras se calcula; llega waveformsChanged).
    std::shared_ptr<const sharkcut::Waveform> waveform(qint64 mediaId);
    bool waveformLoading(qint64 mediaId) const;

    QString projectName() const;
    QString canvasLabel() const;
    QUrl defaultExportFolder() const;
    double meterLeft() const { return m_meter[0]; }
    double meterRight() const { return m_meter[1]; }
    QString projectPath() const { return m_projectPath; }
    bool modified() const { return m_history.modified(); }
    bool fileBusy() const { return m_fileBusy; }
    bool exporting() const { return m_exporter != nullptr; }
    double exportProgress() const { return m_exportProgress; }
    QString exportMessage() const { return m_exportMessage; }

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

    // --- Archivo del proyecto (lectura y escritura en segundo plano) ---
    // Proyecto vacío. `width`/`height` = 0: el lienzo lo fija el primer clip; `fps` = 0:
    // también los fotogramas por segundo. `name` se propone al guardar.
    Q_INVOKABLE void newProject(const QString& name = {}, int width = 0, int height = 0,
                                int fps = 0);
    Q_INVOKABLE void openProject(const QUrl& file);
    Q_INVOKABLE void openProjectPath(const QString& path);
    // Guarda en el archivo actual; false si aún no tiene (hay que usar saveAs).
    Q_INVOKABLE bool save();
    Q_INVOKABLE void saveAs(const QUrl& file);

    // Exporta la línea de tiempo a MP4 (mismo compositor que la vista previa).
    // `quality`: 0 ligera (8 Mbps), 1 alta (16 Mbps), 2 máxima (40 Mbps).
    Q_INVOKABLE void exportTo(const QUrl& file, int quality = 1);
    Q_INVOKABLE void exportToFolder(const QUrl& folder, const QString& name, int quality);
    Q_INVOKABLE void cancelExport();

    // --- Biblioteca ---
    // Analiza los archivos en segundo plano y los añade a la biblioteca. Con
    // `placeAt` >= 0 también los coloca, uno tras otro, desde ese fotograma, en la pista
    // `track` (índice; -1 = V1 y A1) y su pareja del otro tipo (V2 ↔ A2…).
    Q_INVOKABLE void importFiles(const QList<QUrl>& urls, qint64 placeAt = -1, int track = -1);
    Q_INVOKABLE void importPaths(const QStringList& paths, qint64 placeAt = -1, int track = -1);
    Q_INVOKABLE void removeMedia(qint64 mediaId);
    Q_INVOKABLE QString mediaPath(qint64 mediaId) const;

    // --- Pistas ---
    Q_INVOKABLE void addTrack(const QString& kind); // "video" (encima) o "audio" (debajo)
    Q_INVOKABLE void removeTrack(int track);
    Q_INVOKABLE void setTrackHidden(int track, bool hidden);
    Q_INVOKABLE void setTrackMuted(int track, bool muted);
    Q_INVOKABLE void setTrackLocked(int track, bool locked);

    // --- Edición ---
    // Coloca el medio entero en `frame` (sobrescribiendo); `frame` < 0: al final.
    // `track`: pista donde se suelta (índice; -1 = V1 y A1), como en importFiles.
    Q_INVOKABLE void addMedia(qint64 mediaId, qint64 frame = -1, int track = -1);
    // Coloca el tramo marcado del reproductor de origen: al final o en el cabezal.
    Q_INVOKABLE void addFromSource(Player* source, bool atPlayhead);
    Q_INVOKABLE void splitAtPlayhead();                      // divide lo que hay bajo el cabezal
    Q_INVOKABLE void splitClip(qint64 clipId, qint64 frame); // cuchilla
    Q_INVOKABLE void removeSelection();
    Q_INVOKABLE void trimStart(qint64 clipId, qint64 frame);
    Q_INVOKABLE void trimEnd(qint64 clipId, qint64 frame);
    Q_INVOKABLE qint64 limitTrimStart(qint64 clipId, qint64 frame) const;
    Q_INVOKABLE qint64 limitTrimEnd(qint64 clipId, qint64 frame) const;
    // Mueve el clip (o toda la selección, si el clip está en ella) `delta` fotogramas y,
    // los clips del tipo de `clipId`, `trackShift` pistas (+1 = V2 → V3 o A1 → A2).
    Q_INVOKABLE void moveClips(qint64 clipId, qint64 delta, int trackShift = 0);
    Q_INVOKABLE qint64 limitMove(qint64 clipId, qint64 delta) const;
    Q_INVOKABLE int limitTrackShift(qint64 clipId, int trackShift) const;

    // Posición, escala, opacidad y recorte de un clip de video. `values` puede traer
    // solo algunas claves (x, y, scale, opacity, cropLeft, cropTop, cropRight, cropBottom).
    // previewTransform() lo muestra en el visor sin tocar el proyecto (mientras se
    // arrastra); setClipTransform() lo aplica como un paso de deshacer.
    Q_INVOKABLE void previewTransform(qint64 clipId, const QVariantMap& values);
    Q_INVOKABLE void endPreview();
    Q_INVOKABLE void setClipTransform(qint64 clipId, const QVariantMap& values,
                                      const QString& label = {});
    // Ajusta el clip al lienzo (`fill` = false: entero, con bandas) o lo rellena (sin
    // bandas, recortando lo que sobre), centrado y respetando su recorte.
    Q_INVOKABLE void fitClip(qint64 clipId, bool fill);
    Q_INVOKABLE void resetTransform(qint64 clipId);
    // Clip de video visible más alto bajo un punto del lienzo (píxeles), o 0.
    Q_INVOKABLE qint64 clipAtCanvas(double x, double y) const;
    // Volumen de un clip de audio, en dB.
    Q_INVOKABLE void setClipVolume(qint64 clipId, double volumeDb);
    // Separa el video y el audio de los clips seleccionados.
    Q_INVOKABLE void unlinkSelection();
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
    void fileChanged();
    void modifiedChanged();
    void fileBusyChanged();
    void saved(bool ok);
    void opened(bool ok);
    void exportChanged();
    void metersChanged();
    void selectionChanged();
    void selectedClipChanged();
    void waveformsChanged();
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
    // Reproducción de una pista de video: el clip que se ve y el siguiente preparado.
    struct TrackVideo {
        std::array<VideoSlot, 2> decoders;
        int current = -1; // hueco con el clip que se ve
        std::shared_ptr<const sharkcut::VideoFrame> frame;
        sharkcut::ObjectId frameClip = 0; // clip al que pertenece `frame`
    };

    // Aplica una edición registrándola en el historial.
    bool edit(const QString& label, const std::function<bool(sharkcut::Project&)>& change);
    void onProjectChanged(); // tras editar, deshacer o rehacer
    void restartAudio();
    void resetVideoSlots();
    std::vector<std::size_t> playedVideoTracks() const; // pistas de video visibles

    std::int64_t sourceIndex(const sharkcut::Clip& clip, std::int64_t frame) const;
    int slotFor(std::size_t track, const sharkcut::Clip& clip, int avoid);
    void placeSlot(std::size_t track, int slot, const sharkcut::Clip& clip, std::int64_t frame,
                   bool force);
    const sharkcut::Clip* nextVideoClip(std::size_t track, std::int64_t frame) const;
    void prepareNext(std::size_t track, std::int64_t frame);
    void requestFrames(qint64 frame); // colocar los hilos para mostrar `frame`
    bool takeFrames(qint64 frame);    // recoger los fotogramas de `frame`; true si están
    bool takeTrackFrame(std::size_t track, qint64 frame);
    void onFramesAvailable();

    sharkcut::PlaceTarget placeTarget(int track) const;
    sharkcut::ClipTransform transformOf(sharkcut::ObjectId clip) const; // con la vista previa
    static sharkcut::ClipTransform withValues(sharkcut::ClipTransform transform,
                                              const QVariantMap& values);
    // Rectángulo visible del clip en el lienzo (píxeles) y su tamaño sin recortar.
    struct CanvasBox {
        double left = 0, top = 0, width = 0, height = 0;
        double fullWidth = 0, fullHeight = 0;
    };
    CanvasBox canvasBox(const sharkcut::Clip& clip, const sharkcut::ClipTransform& t) const;
    void updateWaveformDirectory();

    void setCurrentFrame(qint64 frame);
    void setPlaying(bool playing);
    void setBusy(bool busy);
    void setError(const QString& message);
    void finishImport(std::vector<sharkcut::MediaItem> items, QStringList errors, qint64 placeAt,
                      int track);
    void saveToPath(const QString& path);
    void finishSave(const std::shared_ptr<const sharkcut::Project>& written, const QString& path,
                    const QString& error);
    void finishOpen(const std::shared_ptr<sharkcut::Project>& project, const QString& path,
                    const QString& error, const QStringList& missing);
    void setFileBusy(bool busy);

    sharkcut::History m_history;
    QString m_projectPath; // vacío: proyecto sin guardar
    bool m_fileBusy = false;
    std::vector<sharkcut::ObjectId> m_selection;
    int m_importing = 0;

    std::vector<TrackVideo> m_video; // por índice de pista (las de audio no se usan)
    // Estado del proyecto con el que se prepararon los hilos y el audio.
    std::shared_ptr<const sharkcut::Project> m_prepared;
    // Transformación provisional mientras se arrastra en el visor o en Propiedades.
    sharkcut::ObjectId m_previewClip = 0;
    sharkcut::ClipTransform m_previewTransform;
    WaveformCache m_waveforms;

    std::unique_ptr<sharkcut::AudioOutput> m_audioOutput;
    std::unique_ptr<sharkcut::AudioWorker> m_audioWorker;
    PlaybackClock m_clock;

    qint64 m_currentFrame = 0;
    qint64 m_pendingFrame = -1; // fotograma pedido en pausa que aún no ha llegado
    bool m_playing = false;
    bool m_busy = false;
    bool m_muted = false;
    QString m_errorString;

    std::unique_ptr<QOffscreenSurface> m_exportSurface; // solo OpenGL
    std::unique_ptr<sharkcut::SequenceExporter> m_exporter;
    quint64 m_exportSession = 0;
    double m_exportProgress = 0.0;
    QString m_exportMessage;

    QString m_newName; // nombre propuesto de un proyecto aún sin guardar
    std::array<double, 2> m_meter{-60.0, -60.0};
    QElapsedTimer m_meterTimer;
    void updateMeters();
};
