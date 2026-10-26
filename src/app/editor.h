#pragma once

#include "compositionsource.h"
#include "core/captions.h"
#include "core/document.h"
#include "core/edit.h"
#include "core/history.h"
#include "core/project.h"
#include "core/project_file.h"
#include "media/decode_worker.h"
#include "playbackclock.h"
#include "proxies.h"
#include "render/caption_renderer.h"
#include "waveforms.h"

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QThreadPool>
#include <QTimer>
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

// Editor: los proyectos abiertos (cada uno con su biblioteca, carpetas y secuencias), las
// pestañas de secuencias de la línea de tiempo, las herramientas de edición y la
// reproducción de la secuencia activa en el visor.
//
// Hay siempre al menos un proyecto abierto. El proyecto activo es el del panel de
// medios; la pestaña activa, si la hay, es una de sus secuencias (al cambiar de pestaña
// a la de otro proyecto, ese pasa a ser el activo). Guardar, deshacer y el título de la
// ventana se refieren al proyecto activo.
//
// Toda modificación de un proyecto pasa por su historial (deshacer/rehacer).
// Reproducción: por cada pista de video visible, dos hilos de decodificación, uno para
// el clip que se ve y otro preparado en el primer fotograma del clip siguiente, así los
// cortes no se atascan. La mezcla de audio de la línea de tiempo marca el reloj.
class Editor : public CompositionSource {
    Q_OBJECT
    QML_ELEMENT

    // Proyectos abiertos: {uid, name, modified, active, saved}
    Q_PROPERTY(QVariantList projects READ projects NOTIFY projectsChanged)
    Q_PROPERTY(qint64 activeProject READ activeProject NOTIFY projectsChanged)
    // Pestañas de secuencias: {index, project, sequence, name, projectName, active}
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY projectsChanged)
    Q_PROPERTY(int activeTab READ activeTab NOTIFY projectsChanged)
    Q_PROPERTY(bool hasSequence READ hasSequence NOTIFY projectsChanged)
    Q_PROPERTY(QString sequenceName READ sequenceName NOTIFY projectsChanged)
    // Biblioteca del proyecto activo: carpetas {id, name, count}, secuencias {id, name,
    // folder, canvas, details, duration, open} y medios (con su carpeta)
    Q_PROPERTY(QVariantList folders READ folders NOTIFY libraryChanged)
    Q_PROPERTY(QVariantList sequences READ sequences NOTIFY libraryChanged)
    // libraryChanged solo llega si cambió alguna de las tres listas.
    Q_PROPERTY(QVariantList mediaItems READ mediaItems NOTIFY libraryChanged)
    // Proxy de cada medio que tiene o espera uno (clave: id del medio como texto):
    // {state: "queued" | "working" | "ready" | "failed", progress 0–1}. Va aparte de
    // mediaItems para que el progreso no reconstruya la lista de Medios.
    Q_PROPERTY(QVariantMap proxyStates READ proxyStates NOTIFY proxiesChanged)
    // Proxies: usarlos en la vista previa (la exportación usa siempre los originales),
    // si se pueden crear (el proyecto tiene archivo) y el estado de los que se crean.
    Q_PROPERTY(bool useProxies READ useProxies WRITE setUseProxies NOTIFY proxiesChanged)
    // Duración (s) con la que se importan las imágenes fijas (PNG, JPG…).
    Q_PROPERTY(
        double stillSeconds READ stillSeconds WRITE setStillSeconds NOTIFY stillSecondsChanged)
    Q_PROPERTY(bool proxiesAvailable READ proxiesAvailable NOTIFY proxiesChanged)
    Q_PROPERTY(QString proxyStatus READ proxyStatus NOTIFY proxiesChanged)
    // Videos del proyecto que pueden tener proxy y cuántos lo tienen ya listo.
    Q_PROPERTY(int proxiesTotal READ proxiesTotal NOTIFY proxiesChanged)
    Q_PROPERTY(int proxiesReady READ proxiesReady NOTIFY proxiesChanged)
    // Pistas en el orden de `tracks` del proyecto: {index, name, kind, number, hidden,
    // muted, locked}
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY projectChanged)
    // Transiciones de la línea de tiempo: {id, track, cut, before, after, name, sound}
    Q_PROPERTY(QVariantList transitions READ transitions NOTIFY projectChanged)
    // Catálogos para la interfaz: transiciones, curvas y efectos de sonido ("Ninguno" el 0)
    Q_PROPERTY(QStringList transitionNames READ transitionNames CONSTANT)
    Q_PROPERTY(QStringList easingNames READ easingNames CONSTANT)
    // Curvas de los keyframes (índices de setKeyframeEasing)
    Q_PROPERTY(QStringList keyframeEasingNames READ keyframeEasingNames CONSTANT)
    Q_PROPERTY(QStringList soundNames READ soundNames CONSTANT)
    // Biblioteca de efectos de sonido (pestaña Sonidos), por grupos:
    // [{id, name, category, seconds}].
    Q_PROPERTY(QVariantList soundLibrary READ soundLibrary CONSTANT)
    // Sonido que lleva por defecto cada transición ("" = ninguno), en el orden de transitionNames
    Q_PROPERTY(QStringList transitionSounds READ transitionSounds CONSTANT)
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
    // La transición seleccionada (vacío si no hay): ver selectedTransition().
    Q_PROPERTY(QVariantMap selectedTransition READ selectedTransition NOTIFY selectedClipChanged)
    // Archivo del proyecto
    Q_PROPERTY(QString projectName READ projectName NOTIFY fileChanged)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY fileChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
    Q_PROPERTY(bool fileBusy READ fileBusy NOTIFY fileBusyChanged)
    Q_PROPERTY(bool detectingKey READ detectingKey NOTIFY detectingKeyChanged)
    // Subtítulos. Pistas {index, name ("S1"), hidden, locked, count}; subtítulos
    // seleccionados (ids); el seleccionado para Propiedades (ver selectedCaption()); y los
    // ajustes de estilo guardados {name, builtin, style} (los de fábrica primero).
    Q_PROPERTY(QVariantList captionTracks READ captionTracks NOTIFY projectChanged)
    Q_PROPERTY(QVariantList captionSelection READ captionSelection NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedCaption READ selectedCaption NOTIFY selectedClipChanged)
    Q_PROPERTY(QVariantList captionPresets READ captionPresets NOTIFY captionPresetsChanged)
    // Punto de anclaje (editor_anchor.cpp).
    Q_PROPERTY(bool anchorPicking READ anchorPicking NOTIFY anchorChanged)
    Q_PROPERTY(QVariantMap anchorTargetBox READ anchorTargetBox NOTIFY anchorChanged)
    Q_PROPERTY(bool anchoring READ anchoring NOTIFY anchorChanged)
    Q_PROPERTY(double anchorProgress READ anchorProgress NOTIFY anchorChanged)
    Q_PROPERTY(QString anchorMessage READ anchorMessage NOTIFY anchorChanged)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY clipboardChanged)
    // Ajustes guardados de clips (editor_presets.cpp).
    Q_PROPERTY(QVariantList clipPresets READ clipPresets NOTIFY clipPresetsChanged)
    // Transcripción automática en curso (descarga del modelo incluida)
    Q_PROPERTY(bool transcribing READ transcribing NOTIFY transcriptionChanged)
    Q_PROPERTY(double transcribeProgress READ transcribeProgress NOTIFY transcriptionChanged)
    Q_PROPERTY(QString transcribeMessage READ transcribeMessage NOTIFY transcriptionChanged)
    // Modelos de transcripción [alta, rápida]: {downloaded, megabytes}. Vacía hasta que
    // checkSpeechModels() mira la carpeta de modelos (en otro hilo).
    Q_PROPERTY(QVariantList speechModels READ speechModels NOTIFY speechModelsChanged)
    // Lienzo del proyecto: "16:9", "9:16", "1:1"… y si lo fija el primer clip
    Q_PROPERTY(QString canvasLabel READ canvasLabel NOTIFY projectChanged)
    Q_PROPERTY(QSize canvas READ canvasSize NOTIFY projectChanged)
    Q_PROPERTY(QUrl defaultExportFolder READ defaultExportFolder NOTIFY fileChanged)
    // Medidor de la mezcla: nivel de cada canal en dB (−60 = silencio, 0 = máximo)
    Q_PROPERTY(double meterLeft READ meterLeft NOTIFY metersChanged)
    Q_PROPERTY(double meterRight READ meterRight NOTIFY metersChanged)
    // Exportación
    // Últimos ajustes usados (los usa "Exportar rápido"): {codec: "h264"|"h265", scale,
    // fps (0 = los de la secuencia), quality (0 Ligera, 1 Alta, 2 Máxima, 3 a medida),
    // mbps (a medida), audio, audioKbps, folder (url)}
    Q_PROPERTY(QVariantMap exportSettings READ exportSettings NOTIFY exportSettingsChanged)
    // Archivo de la última exportación terminada ("" si no hay)
    Q_PROPERTY(QString lastExportPath READ lastExportPath NOTIFY exportChanged)
    // Proyectos abiertos hace poco: {path, name, folder, exists, date}
    Q_PROPERTY(QVariantList recentProjects READ recentProjects NOTIFY recentProjectsChanged)
    // Copias de recuperación de una sesión que no se cerró bien: {key, name, path, date}
    Q_PROPERTY(QVariantList recoverableProjects READ recoverableProjects NOTIFY recoveryChanged)
    Q_PROPERTY(bool exporting READ exporting NOTIFY exportChanged)
    Q_PROPERTY(double exportProgress READ exportProgress NOTIFY exportChanged)
    Q_PROPERTY(QString exportMessage READ exportMessage NOTIFY exportChanged)
    // Reproducción
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    // Velocidad de reproducción (J/K/L): 0 en pausa, 1 normal, 2/4/8 hacia delante,
    // −1/−2/−4/−8 hacia atrás. Solo suena a velocidad normal.
    Q_PROPERTY(int playbackSpeed READ playbackSpeed NOTIFY playingChanged)
    // Marcadores de la secuencia activa: {id, frame, name}
    Q_PROPERTY(QVariantList markers READ markers NOTIFY projectChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(qint64 currentFrame READ currentFrame NOTIFY currentFrameChanged)
    Q_PROPERTY(QString timecode READ timecode NOTIFY currentFrameChanged)
    Q_PROPERTY(QString durationTimecode READ durationTimecode NOTIFY projectChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)

public:
    explicit Editor(QObject* parent = nullptr);
    ~Editor() override;

    // Vista de la secuencia activa (vacía si no hay ninguna abierta).
    const sharkcut::Project& project() const { return m_view; }

    QVariantList projects() const;
    qint64 activeProject() const { return static_cast<qint64>(m_activeProject); }
    QVariantList tabs() const;
    int activeTab() const { return m_activeTab; }
    bool hasSequence() const { return activeSequenceId() != 0; }
    QString sequenceName() const;
    QVariantList folders() const;
    QVariantList sequences() const;

    QVariantList mediaItems() const;
    QVariantMap proxyStates() const;
    bool useProxies() const { return m_useProxies; }
    void setUseProxies(bool use);
    bool proxiesAvailable() const { return !m_proxies.directory().isEmpty(); }
    QString proxyStatus() const;
    double stillSeconds() const;
    void setStillSeconds(double seconds);
    int proxiesTotal() const { return static_cast<int>(mediaPaths().size()); }
    int proxiesReady() const { return m_proxies.readyCount(mediaPaths()); }
    // Datos de un clip para la línea de tiempo (TimelineClipsModel): {id, link, track,
    // video, media, start, length, in, volumeDb, transformed, name}.
    QVariantMap clipEntry(std::size_t track, const sharkcut::Clip& clip) const;
    QVariantList tracks() const;
    QVariantList transitions() const;
    QStringList transitionNames() const;
    QStringList easingNames() const;
    QStringList keyframeEasingNames() const;
    QStringList soundNames() const;
    QStringList transitionSounds() const;
    QVariantMap selectedTransition() const;
    qint64 frameCount() const { return project().length(); }
    double fps() const;
    QString projectDescription() const;
    bool canUndo() const { return current().history.canUndo(); }
    bool canRedo() const { return current().history.canRedo(); }
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
    QString projectPath() const { return current().path; }
    bool modified() const { return current().history.modified(); }
    bool fileBusy() const { return m_fileBusy; }
    bool exporting() const { return m_exporter != nullptr; }
    QVariantMap exportSettings() const { return m_exportSettings; }
    QString lastExportPath() const { return m_lastExportPath; }
    QVariantList recentProjects() const { return m_recentProjects; }
    QVariantList recoverableProjects() const { return m_recoverable; }
    double exportProgress() const { return m_exportProgress; }
    QString exportMessage() const { return m_exportMessage; }

    bool playing() const { return m_playing; }
    int playbackSpeed() const { return m_playing ? m_speed : 0; }
    QVariantList markers() const;

    // Pista "nueva": al soltar un medio en la zona vacía de la línea de tiempo (encima de
    // la última pista de video o debajo de la última de audio), se crean una pista de
    // video y una de audio nuevas para él (las que necesite).
    static constexpr int kNewTracks = -2;
    bool busy() const { return m_busy; }
    qint64 currentFrame() const { return m_currentFrame; }
    QString timecode() const;
    QString durationTimecode() const;
    bool muted() const { return m_muted; }
    void setMuted(bool muted);
    QString errorString() const { return m_errorString; }

    sharkcut::CompositorInput composition() const override;
    QSize canvasSize() const override;

    // --- Proyectos (lectura y escritura en segundo plano) ---
    // Abre un proyecto nuevo (sin cerrar los demás) con una secuencia. `width`/`height`
    // = 0: el lienzo lo fija el primer clip; `fps` = 0: también los fotogramas por
    // segundo. `name` se propone al guardar y da nombre a la secuencia.
    Q_INVOKABLE void newProject(const QString& name = {}, int width = 0, int height = 0,
                                int fps = 0);
    // Abre un proyecto junto a los demás (si ya está abierto, solo lo activa).
    Q_INVOKABLE void openProject(const QUrl& file);
    Q_INVOKABLE void openProjectPath(const QString& path);
    // Guarda el proyecto activo en su archivo; false si aún no tiene (usar saveAs).
    Q_INVOKABLE bool save();
    Q_INVOKABLE void saveAs(const QUrl& file);
    Q_INVOKABLE void activateProject(qint64 uid);
    // Cierra un proyecto sin preguntar (la interfaz pregunta antes si hay cambios). Si era
    // el último, queda uno nuevo vacío.
    Q_INVOKABLE void closeProject(qint64 uid);
    Q_INVOKABLE bool projectModified(qint64 uid) const;
    // Proyectos con cambios sin guardar (uids), para preguntar al cerrar la ventana.
    Q_INVOKABLE QVariantList unsavedProjects() const;

    // --- Secuencias y pestañas ---
    Q_INVOKABLE void openSequence(qint64 sequenceId); // del proyecto activo
    Q_INVOKABLE void activateTab(int index);
    Q_INVOKABLE void closeTab(int index);
    Q_INVOKABLE void newSequence(const QString& name, int width, int height, int fps);
    Q_INVOKABLE void renameSequence(qint64 sequenceId, const QString& name);
    // Copia de la secuencia (con `vertical`, en un lienzo 9:16) y la abre.
    Q_INVOKABLE void duplicateSequence(qint64 sequenceId, bool vertical);
    Q_INVOKABLE void removeSequence(qint64 sequenceId);

    // --- Carpetas de la biblioteca ---
    Q_INVOKABLE void newFolder(const QString& name);
    Q_INVOKABLE void renameFolder(qint64 folderId, const QString& name);
    Q_INVOKABLE void removeFolder(qint64 folderId);
    // Mueve medios y secuencias (ids) a una carpeta (0 = raíz).
    Q_INVOKABLE void moveToFolder(const QVariantList& items, qint64 folderId);

    // --- Exportación (mismo compositor que la vista previa) ---
    // Exporta una secuencia del proyecto activo. `options`: los de exportSettings y además
    // sequence (id; 0 = la activa), name (sin ".mp4"), rangeStart y rangeEnd (fotogramas
    // de la secuencia; -1 = desde el principio / hasta el final). Los ajustes quedan como
    // los últimos usados. Nunca reemplaza un archivo: si existe, "nombre (2).mp4".
    Q_INVOKABLE void exportSequence(const QVariantMap& options);
    // La secuencia activa entera, con los últimos ajustes, a la última carpeta y con el
    // nombre de la secuencia.
    Q_INVOKABLE void quickExport();
    Q_INVOKABLE void cancelExport();
    // Abre en el explorador la carpeta del último archivo exportado.
    Q_INVOKABLE void showLastExport() const;
    // Resoluciones que se ofrecen para una secuencia: {width, height, scale, label}
    Q_INVOKABLE QVariantList exportSizes(qint64 sequenceId) const;
    // Tasa de video recomendada (Mbps) para una calidad (0–2) y códec ("h264"/"h265").
    Q_INVOKABLE double recommendedMbps(qint64 sequenceId, double scale, int fps, int quality,
                                       const QString& codec) const;
    // Códigos de tiempo de una secuencia (con sus fps). parseTimecode: -1 si no vale.
    Q_INVOKABLE QString sequenceTimecode(qint64 sequenceId, qint64 frame) const;
    Q_INVOKABLE qint64 parseTimecode(qint64 sequenceId, const QString& text) const;

    // --- Proyectos recientes ---
    Q_INVOKABLE void forgetRecentProject(const QString& path);

    // --- Recuperación ---
    // Abre la copia de recuperación (con sus cambios sin guardar; al guardar va a su
    // archivo original, si tenía) o la descarta.
    Q_INVOKABLE void recoverProject(const QString& key);
    Q_INVOKABLE void discardRecovery(const QString& key);

    // --- Biblioteca ---
    // Analiza los archivos en segundo plano y los añade a la biblioteca. Con
    // `placeAt` >= 0 también los coloca, uno tras otro, desde ese fotograma, en la pista
    // `track` (índice; -1 = V1 y A1; kNewTracks = pistas nuevas) y su pareja del otro tipo
    // (V2 ↔ A2…). Los medios
    // nuevos van a la carpeta `folder` (0 = raíz) del proyecto activo.
    Q_INVOKABLE void importFiles(const QList<QUrl>& urls, qint64 placeAt = -1, int track = -1,
                                 qint64 folder = 0);
    Q_INVOKABLE void importPaths(const QStringList& paths, qint64 placeAt = -1, int track = -1,
                                 qint64 folder = 0);
    Q_INVOKABLE void removeMedia(qint64 mediaId);
    // Crea los proxies de esos medios (ids; vacío = todos los videos del proyecto).
    Q_INVOKABLE void createProxies(const QVariantList& mediaIds);
    Q_INVOKABLE void cancelProxies();
    Q_INVOKABLE QString mediaPath(qint64 mediaId) const;

    // --- Pistas ---
    Q_INVOKABLE void addTrack(const QString& kind); // "video" (encima) o "audio" (debajo)
    Q_INVOKABLE void removeTrack(int track);
    Q_INVOKABLE void setTrackHidden(int track, bool hidden);
    Q_INVOKABLE void setTrackMuted(int track, bool muted);
    Q_INVOKABLE void setTrackLocked(int track, bool locked);

    // --- Edición ---
    // Coloca el medio entero en `frame` (sobrescribiendo); `frame` < 0: al final.
    // `track`: pista donde se suelta (índice; -1 = V1 y A1; kNewTracks = pistas nuevas),
    // como en importFiles.
    Q_INVOKABLE void addMedia(qint64 mediaId, qint64 frame = -1, int track = -1);
    Q_INVOKABLE void splitAtPlayhead();                      // divide lo que hay bajo el cabezal
    Q_INVOKABLE void splitClip(qint64 clipId, qint64 frame); // cuchilla
    Q_INVOKABLE void removeSelection();
    // Copiar, cortar y pegar clips (Ctrl+C, Ctrl+X, Ctrl+V): se copian con su audio,
    // transformación, efectos y keyframes; se pegan en el cabezal, en las mismas pistas y
    // sobrescribiendo lo que haya (también en otra secuencia o proyecto), quedan
    // seleccionados y el cabezal va a su final.
    Q_INVOKABLE void copySelection();
    Q_INVOKABLE void cutSelection();
    Q_INVOKABLE void pasteAtPlayhead();
    bool canPaste() const { return !m_clipboard.empty(); }
    // Edición con rizo (cierra el hueco desplazando lo posterior en las pistas no
    // bloqueadas): Q quita desde el borde anterior hasta el cabezal, W desde el cabezal
    // hasta el borde siguiente; rippleDeleteSelection quita la selección (Mayús+Supr).
    Q_INVOKABLE void rippleTrimPrevious();
    Q_INVOKABLE void rippleTrimNext();
    Q_INVOKABLE void rippleDeleteSelection();
    // Hueco de la pista `track` en `frame` (para el menú): true si se puede cerrar.
    Q_INVOKABLE bool hasGapAt(int track, qint64 frame) const;
    Q_INVOKABLE void closeGap(int track, qint64 frame);

    // --- Marcadores ---
    Q_INVOKABLE void addMarkerAtPlayhead(); // M
    Q_INVOKABLE void removeMarker(qint64 markerId);
    Q_INVOKABLE void renameMarker(qint64 markerId, const QString& name);
    Q_INVOKABLE void moveMarker(qint64 markerId, qint64 frame);
    // Lleva el cabezal al marcador siguiente (`direction` > 0) o al anterior.
    Q_INVOKABLE void goToMarker(int direction);
    // Flechas arriba/abajo: el cabezal al corte (inicio o final de un clip, de cualquier
    // pista) anterior (`direction` < 0) o siguiente.
    Q_INVOKABLE void goToEdit(int direction);
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

    // --- Keyframes (en el cabezal). `property`: "x", "y", "scale", "opacity", "cropLeft",
    // "cropTop", "cropRight" o "cropBottom". Con una propiedad animada, cambiar su valor
    // (setClipTransform, el visor) crea o actualiza el keyframe del cabezal.
    Q_INVOKABLE void setPropertyAnimated(qint64 clipId, const QString& property, bool animated);
    // Pone un keyframe en el cabezal (con el valor de ahora) o lo quita si ya hay uno.
    Q_INVOKABLE void toggleKeyframe(qint64 clipId, const QString& property);
    // Lleva el cabezal al keyframe anterior (−1) o siguiente (1) de la propiedad (o de
    // cualquiera, con `property` vacío).
    Q_INVOKABLE void goToKeyframe(qint64 clipId, const QString& property, int direction);
    // Curva (índice de keyframeEasingNames) de los keyframes del cabezal.
    Q_INVOKABLE void setKeyframeEasing(qint64 clipId, int easingIndex);
    // Mueve los keyframes de un fotograma de la línea de tiempo a otro.
    Q_INVOKABLE void moveKeyframes(qint64 clipId, qint64 from, qint64 to);
    // Pantalla verde: las claves keyEnabled, keyColor ("#RRGGBB"), keySimilarity,
    // keySmoothness, keySpill y keyRemove (quita el efecto) de setClipTransform/previewTransform.
    // detectKeyColor() busca el color de fondo del clip en el fotograma que se ve (en otro hilo) y,
    // si lo encuentra, activa la pantalla verde con él; al terminar emite keyColorDetected.
    // `fallbackToGreen`: si no se encuentra, la activa igualmente con el color que tenga.
    Q_INVOKABLE void detectKeyColor(qint64 clipId, bool fallbackToGreen = false);
    // Efecto del panel Efectos aplicado a un clip de video (arrastrándolo o con el clip
    // seleccionado): "croma", "byn", "desenfoque", "esquinas", "sombra", "resplandor" o un
    // ajuste guardado, "ajuste:<nombre>".
    // Los efectos de imagen son las claves grayscaleOn/grayscale, blurOn/blur,
    // cornersOn/corners y shadowOn/shadowColor/shadowOpacity/shadowSoftness/shadowX/
    // shadowY/shadowSpread de setClipTransform. Selecciona el clip para ajustarlo en Propiedades.
    // Si el clip está en la selección, se aplica a todos sus clips de video a la vez (un
    // solo paso de deshacer).
    Q_INVOKABLE void applyEffect(qint64 clipId, const QString& effect);
    // Clip de la pista de video `track` (índice en tracks) en `frame`, o 0.
    Q_INVOKABLE qint64 videoClipAt(int track, qint64 frame) const;
    bool detectingKey() const { return m_detectingKey; }

    // --- Ajustes guardados de clips (editor_presets.cpp): el aspecto de un clip con nombre,
    // guardado en la configuración de la app (vale para todos los proyectos).
    // [{name, parts: {placement, opacity, crop, effects, chroma, animation}, anchor
    // ("start" | "end"), animated, summary}], por nombre.
    QVariantList clipPresets() const;
    // Guarda el aspecto del clip (en el cabezal; con su animación si `options.animation`).
    // `options`: las partes de `parts` (true/false) y anchor. Si ya hay uno con ese nombre,
    // lo sustituye. Devuelve si se guardó.
    Q_INVOKABLE bool saveClipPreset(qint64 clipId, const QString& name, const QVariantMap& options);
    Q_INVOKABLE bool hasClipPreset(const QString& name) const;
    // Lo aplica a un clip de video (también applyEffect con "ajuste:<nombre>").
    Q_INVOKABLE void applyClipPreset(qint64 clipId, const QString& name);
    Q_INVOKABLE bool renameClipPreset(const QString& name, const QString& newName);
    Q_INVOKABLE void removeClipPreset(const QString& name);

    // --- Punto de anclaje (editor_anchor.cpp): el clip sigue algo que se mueve en otro
    // clip de video. Se elige a qué clip se ancla, se marca en el visor un recuadro sobre
    // la zona a seguir (en el cabezal) y se sigue hacia delante en segundo plano; el
    // recorrido se convierte en keyframes de posición del clip (un paso de deshacer).
    // Clips de video a los que se puede anclar `clipId`: los demás que se ven en el
    // cabezal, de arriba abajo: [{id, name, track}].
    Q_INVOKABLE QVariantList anchorTargets(qint64 clipId) const;
    // Empieza a marcar la zona en el visor (anchorPicking) o lo deja.
    Q_INVOKABLE void beginAnchorPick(qint64 clipId, qint64 targetId);
    Q_INVOKABLE void cancelAnchorPick();
    // Recuadro marcado (píxeles del lienzo): empieza el seguimiento.
    Q_INVOKABLE void startAnchor(double left, double top, double width, double height);
    Q_INVOKABLE void cancelAnchor();
    bool anchorPicking() const { return m_anchorPickClip != 0; }
    // Caja (píxeles del lienzo, como selectedClip.box) del clip a seguir mientras se marca.
    QVariantMap anchorTargetBox() const;
    bool anchoring() const { return m_anchor != nullptr; }
    double anchorProgress() const { return m_anchorProgress; }
    // Estado o resultado del último anclaje (vacío si no hay nada que contar).
    QString anchorMessage() const { return m_anchorMessage; }

    // --- Subtítulos (editor_captions.cpp) ---
    QVariantList captionTracks() const;
    QVariantList captionSelection() const;
    // {count, id, track, trackName, locked, start, end, text, words (bool), ownStyle,
    // style (el que se ve), trackStyle}; vacío ({count: 0}) sin selección.
    QVariantMap selectedCaption() const;
    QVariantList captionPresets() const;
    // Pista nueva encima de las demás; devuelve su índice.
    Q_INVOKABLE int addCaptionTrack();
    Q_INVOKABLE void removeCaptionTrack(int track);
    Q_INVOKABLE void setCaptionTrackHidden(int track, bool hidden);
    Q_INVOKABLE void setCaptionTrackLocked(int track, bool locked);
    // Subtítulo nuevo de 3 s en el cabezal, en la pista (−1: la primera; se crea si no
    // hay ninguna). Queda seleccionado.
    Q_INVOKABLE void addCaptionAtPlayhead(int track = -1);
    Q_INVOKABLE void selectCaption(qint64 id, bool add);
    // Mueve el subtítulo (y los demás seleccionados, si lo está) `delta` fotogramas.
    Q_INVOKABLE void moveCaptions(qint64 id, qint64 delta);
    // `delta` limitado para que ninguno de los que se mueven pise a otro.
    Q_INVOKABLE qint64 limitCaptionMove(qint64 id, qint64 delta) const;
    // Dónde puede empezar y acabar: {low, high} (high = −1 si no hay límite).
    Q_INVOKABLE QVariantMap captionRoom(qint64 id) const;
    Q_INVOKABLE void trimCaption(qint64 id, qint64 start, qint64 end);
    Q_INVOKABLE void setCaptionText(qint64 id, const QString& text);
    // Estilo de los seleccionados (`wholeTrack`: de sus pistas enteras). `values` puede
    // traer solo algunas claves: font, size, bold, italic, uppercase, color, outlineColor,
    // outline, shadowColor, shadow, boxColor, box, x, y, maxWidth. previewCaptionStyle()
    // solo lo muestra en el visor (mientras se arrastra un valor).
    Q_INVOKABLE void previewCaptionStyle(const QVariantMap& values, bool wholeTrack);
    Q_INVOKABLE void endCaptionPreview();
    Q_INVOKABLE void setCaptionStyle(const QVariantMap& values, bool wholeTrack,
                                     const QString& label = {});
    Q_INVOKABLE void applyCaptionPreset(const QString& name, bool wholeTrack);
    // Los seleccionados vuelven al estilo de su pista.
    Q_INVOKABLE void resetCaptionStyle();
    // Guarda el estilo del subtítulo seleccionado como ajuste (reemplaza uno con ese nombre).
    Q_INVOKABLE void saveCaptionPreset(const QString& name);
    Q_INVOKABLE void removeCaptionPreset(const QString& name);
    // Vuelve a dividir la pista: {lines: "single"|"double"|"word", maxChars, minSeconds,
    // gapFrames, removePunctuation}.
    Q_INVOKABLE void relayoutCaptionTrack(int track, const QVariantMap& layout);
    Q_INVOKABLE void importSrt(const QUrl& file);
    Q_INVOKABLE void exportSrt(int track, const QUrl& file);

    // --- Transcripción (editor_transcription.cpp) ---
    bool transcribing() const { return m_transcription != nullptr; }
    double transcribeProgress() const { return m_transcribeProgress; }
    QVariantList speechModels() const { return m_speechModels; }
    Q_INVOKABLE void checkSpeechModels();
    QString transcribeMessage() const { return m_transcribeMessage; }
    // Transcribe el audio de la secuencia activa y crea con lo dicho una pista de
    // subtítulos nueva (un paso de deshacer). Las opciones son las del diálogo de
    // relayoutCaptionTrack más {language ("es", "en", "auto"), audioTrack (−1 = toda la
    // mezcla), preset (nombre del ajuste de estilo), quality (0 alta, 1 rápida)}. Si el
    // modelo no está, se descarga antes (una sola vez).
    Q_INVOKABLE void createCaptions(const QVariantMap& options);
    // Abre la carpeta de los registros de las sesiones (para enviar un fallo).
    Q_INVOKABLE void openLogFolder();
    Q_INVOKABLE void cancelTranscription();
    // Clip de video visible más alto bajo un punto del lienzo (píxeles), o 0.
    Q_INVOKABLE qint64 clipAtCanvas(double x, double y) const;
    // Volumen de un clip de audio, en dB.
    Q_INVOKABLE void setClipVolume(qint64 clipId, double volumeDb);
    // Separa el video y el audio de los clips seleccionados.
    Q_INVOKABLE void unlinkSelection();

    // --- Ayudas para la línea de tiempo (sin recorrer los clips desde QML) ---
    // Imán: el borde de clip, marcador, inicio o cabezal más cercano a `frame` a menos de
    // `maxDistance` fotogramas (sin contar los clips `exclude`); si no hay, `frame`.
    // `withPlayhead` = false al mover el propio cabezal.
    Q_INVOKABLE qint64 snapFrame(qint64 frame, const QVariantList& exclude, double maxDistance,
                                 bool withPlayhead = true) const;
    // Corte (fin de un clip = inicio del siguiente) de la pista más cercano a `frame`, a
    // menos de `maxDistance` fotogramas: {track, cut}, o un mapa vacío.
    Q_INVOKABLE QVariantMap nearestCut(int track, qint64 frame, double maxDistance) const;
    // Clips que se mueven juntos al arrastrar `clipId`: la selección entera si el clip
    // está en ella, y siempre con sus enlazados.
    Q_INVOKABLE QVariantList clipGroup(qint64 clipId) const;
    // Primer fotograma y final de un grupo de clips: {start, end}.
    Q_INVOKABLE QVariantMap groupRange(const QVariantList& clips) const;

    // --- Transiciones ---
    // Pone la transición `typeIndex` (de transitionNames) en el corte `cut` de la pista
    // de video `track`, con su duración y su efecto de sonido por defecto.
    Q_INVOKABLE void addTransition(int track, qint64 cut, int typeIndex);
    // Transición como clip (como en Premiere Composer): la transición `typeIndex` soltada
    // en la pista de video `track` en `frame`. Se centra en el corte más cercano (a menos
    // de medio segundo), va a una pista libre (nunca tapa clips) y lleva su sonido. Se
    // aplica a todo lo que hay debajo; se mueve, corta y recorta como un clip.
    Q_INVOKABLE void addEffectClip(int track, qint64 frame, int typeIndex);
    QVariantList soundLibrary() const;
    // Efecto de sonido de la biblioteca soltado en la pista `track` en `frame` (empieza
    // ahí; si no es una pista de audio, en la primera libre). Queda seleccionado.
    Q_INVOKABLE void addSound(const QString& id, int track, qint64 frame);
    // Con su golpe (lo más fuerte del whoosh, el chasquido del clic…) en el cabezal.
    Q_INVOKABLE void addSoundAtPlayhead(const QString& id);
    // Valores de una transición-clip: typeIndex (de transitionNames), intensity y blur
    // (0–1), easingIndex (de easingNames), length (fotogramas). Solo las claves que vengan.
    Q_INVOKABLE void setEffectClip(qint64 clipId, const QVariantMap& values);
    Q_INVOKABLE void removeTransition(qint64 id);
    Q_INVOKABLE void selectTransition(qint64 id);
    // Cambia el tipo (con sus valores y su sonido por defecto).
    Q_INVOKABLE void setTransitionType(qint64 id, int typeIndex);
    // `key`: "intensity" o "blur" (0–1), "easing" (índice de easingNames).
    Q_INVOKABLE void setTransitionValue(qint64 id, const QString& key, double value);
    Q_INVOKABLE void setTransitionLength(qint64 id, qint64 before, qint64 after);
    // `soundIndex`: índice de soundNames (0 = ninguno).
    Q_INVOKABLE void setTransitionSound(qint64 id, int soundIndex);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    Q_INVOKABLE void select(qint64 clipId, bool add);
    // Con la línea de tiempo alejada, los clips (o subtítulos) diminutos se dibujan juntos
    // en un bloque: el que hay en `frame` de la pista o el más cercano a no más de
    // `maxDistance` fotogramas (0 = ninguno), y los datos de un clip como en la línea de
    // tiempo (clipEntry) para su menú.
    Q_INVOKABLE qint64 clipNear(int track, qint64 frame, qint64 maxDistance) const;
    Q_INVOKABLE qint64 captionNear(int track, qint64 frame, qint64 maxDistance) const;
    Q_INVOKABLE QVariantMap clipData(qint64 clipId) const;
    Q_INVOKABLE QVariantMap captionData(qint64 id) const; // {start, length}
    Q_INVOKABLE void clearSelection();
    // Cuadro de selección de la línea de tiempo: los clips de las pistas `tracks` (índices
    // de tracks) que tocan [from, to); si no hay ninguna de esas, los subtítulos de las
    // pistas `captionTracks`. `add`: se suman a lo seleccionado (Mayús o Ctrl).
    Q_INVOKABLE void selectArea(const QVariantList& tracks, const QVariantList& captionTracks,
                                qint64 from, qint64 to, bool add);
    Q_INVOKABLE bool isSelected(qint64 clipId) const;

    // Código de tiempo de un fotograma de la línea de tiempo (para la regla).
    Q_INVOKABLE QString timecodeFor(qint64 frame) const;

    // --- Reproducción ---
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void seek(qint64 frame);
    Q_INVOKABLE void step(int frames);
    // J (`direction` < 0) y L (> 0): reproduce en ese sentido; cada pulsación más dobla
    // la velocidad (hasta 8×). Pulsar el sentido contrario vuelve a 1×. K es pause().
    Q_INVOKABLE void shuttle(int direction);
    // Avanza la reproducción según el reloj; se llama una vez por refresco de pantalla.
    Q_INVOKABLE void advance();

signals:
    void projectChanged();
    void libraryChanged(); // medios de la biblioteca (o el estado de sus proxies)
    void proxiesChanged();
    void stillSecondsChanged();
    void projectsChanged(); // proyectos abiertos, pestañas o secuencias
    void fileChanged();
    void modifiedChanged();
    void fileBusyChanged();
    void detectingKeyChanged();
    void captionPresetsChanged();
    void clipPresetsChanged();
    void anchorChanged();
    void clipboardChanged();
    void transcriptionChanged();
    void speechModelsChanged();
    void captionsCreated(int count);
    void captionsExported(const QString& path);
    void keyColorDetected(bool found);
    // Se aplicó un efecto desde la pestaña Efectos ("byn", "desenfoque", "esquinas",
    // "sombra", "resplandor"): Propiedades lo lleva a la vista.
    void effectApplied(const QString& effect);
    void saved(bool ok);
    void opened(bool ok);
    void exportChanged();
    void exportSettingsChanged();
    void recentProjectsChanged();
    void recoveryChanged();
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
        std::string path; // lo que tiene abierto: el original o su proxy
        std::unique_ptr<sharkcut::DecodeWorker> worker;
        sharkcut::ObjectId clip = 0; // clip para el que está colocado
        std::int64_t seekIndex = -1; // fotograma del medio al que se colocó
    };
    // Reproducción de una pista de video: dos hilos, para el clip que se ve y el
    // siguiente preparado, o para los dos clips de una transición.
    struct TrackVideo {
        std::array<VideoSlot, 2> decoders;
        struct Shown {
            sharkcut::ObjectId clip = 0;
            std::shared_ptr<const sharkcut::VideoFrame> frame;
        };
        std::array<Shown, 2> shown; // último fotograma de cada clip que se ve
        // El más reciente: se sigue mostrando mientras llega el de otro clip.
        std::shared_ptr<const sharkcut::VideoFrame> last;
        std::shared_ptr<const sharkcut::VideoFrame> frameOf(sharkcut::ObjectId clip) const {
            for (const Shown& s : shown) {
                if (s.clip == clip && s.frame) {
                    return s.frame;
                }
            }
            return nullptr;
        }
    };
    // Un clip que hay que mostrar y su fotograma del medio.
    struct Need {
        const sharkcut::Clip* clip = nullptr;
        std::int64_t index = 0;
    };

    // Un proyecto abierto: su documento con su historial y su archivo.
    struct OpenProject {
        quint64 uid = 0;
        sharkcut::DocumentHistory history;
        QString path;    // vacío: proyecto sin guardar
        QString newName; // nombre propuesto de un proyecto aún sin guardar
        // Copia de recuperación (autoguardado): su nombre en la carpeta de recuperación
        // ("" = aún no tiene) y el estado que se copió por última vez.
        QString recoveryKey;
        std::shared_ptr<const sharkcut::Document> autosaved;
    };
    // Una pestaña de la línea de tiempo: una secuencia de un proyecto abierto.
    struct Tab {
        quint64 project = 0;
        sharkcut::ObjectId sequence = 0;
        qint64 frame = 0; // posición del cabezal al salir de la pestaña
    };

    OpenProject& current();
    const OpenProject& current() const;
    OpenProject* findProject(quint64 uid);
    const OpenProject* findProject(quint64 uid) const;
    const sharkcut::Document& document() const { return current().history.state(); }
    sharkcut::ObjectId activeSequenceId() const;
    QString nameOf(const OpenProject& project) const;
    OpenProject& addProject(sharkcut::Document document, QString path, QString newName);
    // Cambia la pestaña activa (-1 = ninguna) y pone al día la vista y la reproducción.
    void switchTo(int tab);
    void refreshView();
    sharkcut::Workspace workspaceOf(const OpenProject& project) const;

    // Aplica una edición de la secuencia activa registrándola en el historial.
    bool edit(const QString& label, const std::function<bool(sharkcut::Project&)>& change);
    // Edición del proyecto activo entero (secuencias, carpetas, biblioteca).
    bool editDocument(const QString& label, const std::function<bool(sharkcut::Document&)>& change);
    void onProjectChanged(); // tras editar, deshacer o rehacer
    void restartAudio();
    void resetVideoSlots();
    std::vector<std::size_t> playedVideoTracks() const; // pistas de video visibles

    std::vector<Need> needsAt(std::size_t track, std::int64_t frame) const;
    // El próximo clip que habrá que mostrar después de `frame` (para prepararlo).
    std::optional<Need> upcoming(std::size_t track, std::int64_t frame) const;
    int slotFor(std::size_t track, const sharkcut::Clip& clip, int avoid);
    int slotWith(std::size_t track, sharkcut::ObjectId clip) const;
    bool continueSlot(std::size_t track, int slot, const Need& need);
    void placeSlot(std::size_t track, int slot, std::int64_t index, bool force);
    // Coloca los hilos de la pista para `frame`. `seeking`: tras un salto (no avanzando).
    void assignTrack(std::size_t track, std::int64_t frame, bool seeking);
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
    // Coloca el efecto de sonido en el corte de la transición (dentro de una edición).
    static bool placeSound(sharkcut::Project& p, sharkcut::ObjectId transition,
                           std::string_view soundId);

    void setCurrentFrame(qint64 frame);
    void advanceShuttle(sharkcut::FrameRate rate);
    void setPlaying(bool playing);
    void setBusy(bool busy);
    void setError(const QString& message);
    void finishImport(quint64 uid, std::vector<sharkcut::MediaItem> items, QStringList errors,
                      qint64 placeAt, int track, qint64 folder);
    // Cierra las pestañas cuya secuencia ya no existe (tras deshacer o quitarla).
    void pruneTabs();
    void saveToPath(quint64 uid, const QString& path);
    // Ajustes de exportación con los valores por defecto donde falten.
    static QVariantMap completeExportSettings(QVariantMap settings);
    void startExport(const sharkcut::Project& sequence, const QVariantMap& settings,
                     const QString& folder, const QString& name, qint64 rangeStart,
                     qint64 rangeEnd);
    // Pone `path` el primero de los recientes y vuelve a leer la lista (en segundo plano).
    void addRecentProject(const QString& path);
    void refreshRecentProjects();
    void finishSave(quint64 uid, const std::shared_ptr<const sharkcut::Document>& written,
                    const QString& path, const QString& error);
    static QString recoveryDirectory();
    void autosave();
    void removeRecovery(const QString& key);
    void scanRecovery();
    // `recoveryKey` y `recoveredName`: al abrir una copia de recuperación.
    void finishOpen(const std::shared_ptr<sharkcut::LoadedDocument>& loaded, const QString& path,
                    const QString& error, const QStringList& missing,
                    const QString& recoveryKey = {}, const QString& recoveredName = {});
    void setFileBusy(bool busy);

    std::vector<std::unique_ptr<OpenProject>> m_projects;
    quint64 m_nextUid = 1;
    quint64 m_activeProject = 0;
    std::vector<Tab> m_tabs;
    int m_activeTab = -1;
    sharkcut::Project m_view; // secuencia activa
    bool m_fileBusy = false;
    std::vector<sharkcut::ObjectId> m_selection;
    sharkcut::ObjectId m_selectedTransition = 0; // excluye la selección de clips
    int m_importing = 0;

    std::vector<TrackVideo> m_video; // por índice de pista (las de audio no se usan)
    // Estado del proyecto con el que se prepararon los hilos y el audio.
    std::shared_ptr<const sharkcut::Project> m_prepared;
    // Transformación provisional mientras se arrastra en el visor o en Propiedades.
    sharkcut::ObjectId m_previewClip = 0;
    sharkcut::ClipTransform m_previewTransform;
    WaveformCache m_waveforms;
    // Lo último que se anunció de la biblioteca (para avisar solo si cambia).
    void updateLibrary();
    QVariantList m_shownMedia;
    QVariantList m_shownFolders;
    QVariantList m_shownSequences;
    ProxyManager m_proxies;
    bool m_useProxies = false;
    // Ruta que se decodifica para la vista previa: el proxy, si se usan y está listo.
    std::string decodePath(const sharkcut::MediaItem& item) const;
    // Cierra los hilos de video y vuelve a pedir el fotograma (cambió qué se decodifica).
    void reloadVideo();
    QStringList mediaPaths() const;

    std::unique_ptr<sharkcut::AudioOutput> m_audioOutput;
    std::unique_ptr<sharkcut::AudioWorker> m_audioWorker;
    PlaybackClock m_clock;

    qint64 m_currentFrame = 0;
    qint64 m_pendingFrame = -1; // fotograma pedido en pausa que aún no ha llegado
    bool m_playing = false;
    // J/K/L: a una velocidad distinta de 1 no suena y el tiempo lo lleva un temporizador
    // desde `m_shuttleFrom`.
    int m_speed = 1;
    QElapsedTimer m_shuttleTimer;
    qint64 m_shuttleFrom = 0;
    bool m_busy = false;
    bool m_muted = false;
    QString m_errorString;

    std::unique_ptr<QOffscreenSurface> m_exportSurface; // solo OpenGL
    std::unique_ptr<sharkcut::SequenceExporter> m_exporter;
    QVariantMap m_exportSettings;
    QString m_lastExportPath;
    QVariantList m_recentProjects;
    QVariantList m_recoverable;
    QTimer m_autosaveTimer;
    // Propiedades de un clip animado durante la reproducción: unas pocas veces por
    // segundo, no en cada fotograma (rehacer el panel 60 veces por segundo atascaba).
    QTimer m_selectedClipTimer;
    sharkcut::Clipboard m_clipboard; // clips copiados (para todos los proyectos abiertos)
    bool m_detectingKey = false;
    // Clips a los que va la pantalla verde que se está detectando (vacío: solo ese clip).
    std::vector<sharkcut::ObjectId> m_keyTargets;
    // Clips de video a los que va un efecto soltado sobre `clip`: los seleccionados si él
    // lo está (sin pistas bloqueadas), si no solo él.
    std::vector<sharkcut::ObjectId> effectTargets(sharkcut::ObjectId clip) const;
    // Los mismos valores (claves de setClipTransform) a varios clips, en un paso.
    void setClipsTransform(const std::vector<sharkcut::ObjectId>& clips, const QVariantMap& values,
                           const QString& label);
    mutable sharkcut::CaptionImageCache m_captionImages; // subtítulos ya dibujados
    std::vector<sharkcut::ObjectId> m_selectedCaptions;
    // Para Propiedades: por propiedad, {animated, key (hay keyframe en el cabezal), prev,
    // next}; y keyEasing: índice de la curva del keyframe del cabezal (−1 si no hay).
    QVariantMap animationInfo(const sharkcut::Clip& clip) const;
    // Medidas de división del diálogo de subtítulos y estilo de un ajuste por su nombre
    // (el de fábrica «Clásico» si no existe).
    static sharkcut::CaptionLayout captionLayoutFrom(const QVariantMap& values);
    sharkcut::CaptionStyle presetStyle(const QString& name) const;
    // Estilo provisional mientras se arrastra un valor en Propiedades.
    struct CaptionPreview {
        std::vector<sharkcut::ObjectId> captions;
        std::vector<std::size_t> tracks; // pistas enteras
        sharkcut::CaptionStyle style;
    };
    std::optional<CaptionPreview> m_captionPreview;
    // Transcripción en curso: lo que hace falta para terminarla (ver createCaptions).
    struct AnchorJob;
    std::shared_ptr<AnchorJob> m_anchor;
    sharkcut::ObjectId m_anchorPickClip = 0;
    sharkcut::ObjectId m_anchorPickTarget = 0;
    double m_anchorProgress = 0.0;
    QString m_anchorMessage;
    void finishAnchor(const std::shared_ptr<AnchorJob>& job);

    struct Transcription;
    std::shared_ptr<Transcription> m_transcription;
    double m_transcribeProgress = 0.0;
    QVariantList m_speechModels;
    QString m_transcribeMessage;
    void setTranscribeState(double progress, const QString& message);
    // Al cerrar: cancela la transcripción y espera a que pare la descarga.
    void shutdownTranscription();
    void startDownload(const std::shared_ptr<Transcription>& job);
    void startTranscription(const std::shared_ptr<Transcription>& job);
    void finishTranscription(const std::shared_ptr<Transcription>& job,
                             std::vector<sharkcut::TimedWord> words, const QString& error);
    // Estilo resultante de aplicar `values` al del primer subtítulo seleccionado.
    std::optional<sharkcut::CaptionStyle> captionStyleWith(const QVariantMap& values,
                                                           bool wholeTrack) const;
    std::vector<std::size_t> selectedCaptionTracks() const;
    void pruneCaptionSelection();
    QThreadPool m_recoveryPool; // un hilo: escrituras y borrados en orden
    quint64 m_exportSession = 0;
    double m_exportProgress = 0.0;
    QString m_exportMessage;

    std::array<double, 2> m_meter{-60.0, -60.0};
    QElapsedTimer m_meterTimer;
    void updateMeters();
};
