#include "editor.h"

#include "logging.h"

#include "audio/audio_output.h"
#include "audio/audio_worker.h"
#include "core/edit.h"
#include "core/export_settings.h"
#include "core/motion_anchor.h"
#include "core/project_file.h"
#include "core/sfx.h"
#include "media/chroma_key.h"
#include "media/media_error.h"
#include "media/media_info.h"
#include "media/mp4_writer.h"
#include "media/timeline_audio.h"
#include "media/video_decoder.h"
#include "render/sequence_exporter.h"

#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMetaObject>
#include <QOffscreenSurface>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QThreadPool>
#include <QUuid>
#include <QVariantMap>
#include <rhi/qrhi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <limits>
#include <numbers>
#include <numeric>

using sharkcut::Clip;
using sharkcut::DecodeWorker;
using sharkcut::MediaItem;
using sharkcut::ObjectId;
using sharkcut::Project;

namespace {

// Ajustes de la aplicación (QSettings).
constexpr auto kExportSettingsKey = "exportar/ajustes";
constexpr auto kRecentProjectsKey = "proyectos/recientes";
constexpr int kMaxRecentProjects = 15;
// Cada cuánto se hace la copia de recuperación (SHARKCUT_AUTOSAVE_MS la cambia, para
// probarlo).
int autosaveIntervalMs() {
    bool ok = false;
    const int value = qEnvironmentVariableIntValue("SHARKCUT_AUTOSAVE_MS", &ok);
    return ok && value > 0 ? value : 2 * 60 * 1000;
}

sharkcut::VideoCodec codecOf(const QString& name) {
    return name == QLatin1String("h265") ? sharkcut::VideoCodec::H265 : sharkcut::VideoCodec::H264;
}

// Duración legible: "3:35" o "1:02:03".
QString durationText(std::int64_t us) {
    const std::int64_t total = us / 1'000'000;
    const std::int64_t h = total / 3600;
    const std::int64_t m = (total / 60) % 60;
    const std::int64_t s = total % 60;
    if (h > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(h)
            .arg(m, 2, 10, QLatin1Char('0'))
            .arg(s, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

QString fpsText(sharkcut::FrameRate rate) {
    if (rate.den == 1) {
        return QString::number(rate.num);
    }
    return QString::number(static_cast<double>(rate.num) / static_cast<double>(rate.den), 'f', 2);
}

// Con la línea de tiempo vacía, el primer clip con video fija la resolución y los
// fotogramas por segundo del proyecto (como al crear una secuencia desde un clip).
void adoptSettings(Project& project, ObjectId media) {
    const MediaItem* item = project.findMedia(media);
    // Una imagen no decide el lienzo ni los fps (una foto de 4000×3000 a «25 fps»…).
    if (project.length() > 0 || !item || !item->hasVideo || item->still || item->width <= 0 ||
        item->height <= 0) {
        return;
    }
    if (project.settings.adoptFrameRate) {
        project.settings.frameRate = item->frameRate;
    }
    if (project.settings.adoptCanvas) {
        project.settings.width = item->width;
        project.settings.height = item->height;
    }
}

ObjectId findMediaByPath(const Project& project, const std::string& path) {
    for (const MediaItem& item : project.media) {
        if (item.path == path) {
            return item.id;
        }
    }
    return 0;
}

QString fromView(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// Carpeta de los efectos de sonido, junto al ejecutable.
// Nombre de un texto libre: su primera línea con algo (o «Texto» si está vacío).
QString textClipName(const sharkcut::TextClip& text) {
    for (const QString& line : QString::fromStdString(text.text).split(QLatin1Char('\n'))) {
        if (!line.trimmed().isEmpty()) {
            return line.trimmed();
        }
    }
    return QStringLiteral("Texto");
}

QString soundFolder() {
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("presets/sfx"));
}

// Los efectos de sonido que una edición acaba de añadir a la biblioteca (ids desde
// `firstNew`) van a la carpeta de los recursos de SharkCut, que se crea si no está.
void fileNewResources(sharkcut::Document& document, ObjectId firstNew) {
    const std::string folder = QDir(soundFolder()).absolutePath().toStdString();
    std::vector<ObjectId> added;
    for (const MediaItem& item : document.media) {
        if (item.id >= firstNew && item.folder == 0 &&
            QFileInfo(QString::fromStdString(item.path)).absolutePath().toStdString() == folder) {
            added.push_back(item.id);
        }
    }
    if (!added.empty()) {
        sharkcut::moveToFolder(document, added,
                               sharkcut::resourceFolder(document, "Recursos de SharkCut"));
    }
}

// Medio de la biblioteca para un efecto de sonido (se añade si no está).
ObjectId soundMedia(Project& project, const sharkcut::SoundEffect& effect) {
    const std::string path = QDir(soundFolder()).filePath(fromView(effect.file)).toStdString();
    if (const ObjectId id = findMediaByPath(project, path)) {
        return id;
    }
    MediaItem item;
    item.path = path;
    item.name = std::string(effect.file);
    item.durationUs = effect.durationUs;
    item.frameRate = project.settings.frameRate;
    item.hasAudio = true;
    return sharkcut::addMedia(project, item);
}

// Volumen al que entra un efecto de sonido (por debajo de la voz y el juego).
constexpr double kSoundVolumeDb = -6.0;

// Documento nuevo con una secuencia (V1 y V2 encima, A1 y A2 debajo).
sharkcut::Document newDocument(const std::string& sequenceName, int width, int height, int fps) {
    sharkcut::ProjectSettings settings;
    if (width > 0 && height > 0) {
        settings.width = width;
        settings.height = height;
        settings.adoptCanvas = false;
    }
    if (fps > 0) {
        settings.frameRate = {fps, 1};
        settings.adoptFrameRate = false;
    }
    sharkcut::Document document;
    sharkcut::addSequence(document, sequenceName, settings);
    return document;
}

QString aspectLabel(int w, int h) {
    const int g = std::gcd(w, h);
    if (g <= 0) {
        return {};
    }
    const int rw = w / g;
    const int rh = h / g;
    if ((rw == 16 && rh == 9) || (rw == 9 && rh == 16) || (rw == 1 && rh == 1) ||
        (rw == 4 && rh == 3)) {
        return QStringLiteral("%1:%2").arg(rw).arg(rh);
    }
    return QStringLiteral("%1×%2").arg(w).arg(h);
}

// Mismo tramo del mismo medio (los ids pueden cambiar: dividir crea uno nuevo).
bool sameTiming(const Clip& a, const Clip& b) {
    return a.media == b.media && a.start == b.start && a.in == b.in && a.length == b.length;
}

// ¿Se lee lo mismo en las dos pistas? Las dos mitades de un corte cuentan como el clip
// entero: dividir durante la reproducción no reinicia nada.
template <typename Same>
bool sameClips(const sharkcut::Track& a, const sharkcut::Track& b, Same same) {
    const std::vector<Clip> ca = sharkcut::continuousClips(a);
    const std::vector<Clip> cb = sharkcut::continuousClips(b);
    return std::equal(ca.begin(), ca.end(), cb.begin(), cb.end(), same);
}

// ¿Cambia lo que deben decodificar los hilos de video? (No: posición, escala, audio…)
bool sameVideo(const Project& a, const Project& b) {
    if (a.settings.frameRate != b.settings.frameRate || a.media != b.media ||
        a.tracks.size() != b.tracks.size() || a.length() != b.length()) {
        return false;
    }
    for (std::size_t t = 0; t < a.tracks.size(); ++t) {
        const sharkcut::Track& ta = a.tracks[t];
        const sharkcut::Track& tb = b.tracks[t];
        if (ta.kind != tb.kind || ta.hidden != tb.hidden) {
            return false;
        }
        if (ta.kind == sharkcut::TrackKind::Video && !sameClips(ta, tb, sameTiming)) {
            return false;
        }
        // Lo que dura cada transición cambia qué fotogramas hacen falta (su tipo, no).
        if (!std::equal(ta.transitions.begin(), ta.transitions.end(), tb.transitions.begin(),
                        tb.transitions.end(),
                        [](const sharkcut::Transition& x, const sharkcut::Transition& y) {
                            return x.from == y.from && x.to == y.to && x.before == y.before &&
                                   x.after == y.after;
                        })) {
            return false;
        }
    }
    return true;
}

// ¿Cambia lo que suena?
bool sameAudio(const Project& a, const Project& b) {
    if (a.settings.frameRate != b.settings.frameRate || a.media != b.media ||
        a.tracks.size() != b.tracks.size() || a.length() != b.length()) {
        return false;
    }
    for (std::size_t t = 0; t < a.tracks.size(); ++t) {
        const sharkcut::Track& ta = a.tracks[t];
        const sharkcut::Track& tb = b.tracks[t];
        if (ta.kind != tb.kind || ta.muted != tb.muted) {
            return false;
        }
        if (ta.kind == sharkcut::TrackKind::Audio &&
            !sameClips(ta, tb, [](const Clip& x, const Clip& y) {
                return sameTiming(x, y) && x.volumeDb == y.volumeDb;
            })) {
            return false;
        }
    }
    return true;
}

QVariantMap transformToMap(const sharkcut::ClipTransform& t) {
    return {
        {QStringLiteral("x"), t.x},
        {QStringLiteral("y"), t.y},
        {QStringLiteral("scale"), t.scale},
        {QStringLiteral("rotation"), t.rotation},
        {QStringLiteral("opacity"), t.opacity},
        {QStringLiteral("cropLeft"), t.cropLeft},
        {QStringLiteral("cropTop"), t.cropTop},
        {QStringLiteral("cropRight"), t.cropRight},
        {QStringLiteral("cropBottom"), t.cropBottom},
        {QStringLiteral("effects"),
         QVariantMap{
             {QStringLiteral("grayscaleOn"), t.effects.grayscaleOn},
             {QStringLiteral("grayscale"), t.effects.grayscale},
             {QStringLiteral("blurOn"), t.effects.blurOn},
             {QStringLiteral("blur"), t.effects.blur},
             {QStringLiteral("cornersOn"), t.effects.cornersOn},
             {QStringLiteral("corners"), t.effects.corners},
             {QStringLiteral("shadowOn"), t.effects.shadowOn},
             {QStringLiteral("shadowColor"),
              QStringLiteral("#%1").arg(t.effects.shadowColor, 6, 16, QLatin1Char('0')).toUpper()},
             {QStringLiteral("shadowOpacity"), t.effects.shadowOpacity},
             {QStringLiteral("shadowSoftness"), t.effects.shadowSoftness},
             {QStringLiteral("shadowX"), t.effects.shadowX},
             {QStringLiteral("shadowY"), t.effects.shadowY},
             {QStringLiteral("shadowSpread"), t.effects.shadowSpread},
             {QStringLiteral("tintOn"), t.effects.tintOn},
             {QStringLiteral("tintColor"),
              QStringLiteral("#%1").arg(t.effects.tintColor, 6, 16, QLatin1Char('0')).toUpper()},
             {QStringLiteral("tint"), t.effects.tint},
         }},
        {QStringLiteral("chromaKey"),
         QVariantMap{
             // El clip tiene el efecto (aunque esté desactivado): se muestra en Propiedades.
             {QStringLiteral("added"), t.chromaKey != sharkcut::ChromaKey{}},
             {QStringLiteral("enabled"), t.chromaKey.enabled},
             {QStringLiteral("color"),
              QStringLiteral("#%1").arg(t.chromaKey.color, 6, 16, QLatin1Char('0')).toUpper()},
             {QStringLiteral("similarity"), t.chromaKey.similarity},
             {QStringLiteral("smoothness"), t.chromaKey.smoothness},
             {QStringLiteral("spill"), t.chromaKey.spill},
         }},
    };
}

} // namespace

Editor::Editor(QObject* parent) : CompositionSource(parent) {
    m_audioOutput = createAudioOutput(nullptr);
    // Siempre hay un proyecto abierto: al empezar, uno nuevo con una secuencia.
    OpenProject& first = addProject(newDocument("Secuencia 1", 0, 0, 0), {}, {});
    m_activeProject = first.uid;
    m_tabs.push_back({first.uid, first.history.state().sequences.front().id, 0});
    m_selectedClipTimer.setSingleShot(true);
    m_selectedClipTimer.setInterval(150);
    m_previewPool.setMaxThreadCount(1);
    m_previewTimer.setInterval(100);
    connect(&m_previewTimer, &QTimer::timeout, this, [this] {
        // Al acabar el sonido, el botón vuelve a «Escuchar».
        if (m_previewStarted && m_soundPreview && !m_soundPreview->playing()) {
            stopSoundPreview();
        }
    });
    connect(&m_selectedClipTimer, &QTimer::timeout, this, &Editor::selectedClipChanged);
    m_activeTab = 0;
    refreshView();
    resetVideoSlots();
    connect(&m_waveforms, &WaveformCache::ready, this, &Editor::waveformsChanged);
    // La biblioteca solo avisa si cambió lo que muestra: cada aviso reconstruye la lista de
    // Medios (vuelve arriba y corta un arrastre en curso).
    connect(this, &Editor::projectChanged, this, &Editor::updateLibrary);
    connect(this, &Editor::projectsChanged, this, &Editor::updateLibrary);
    connect(this, &Editor::projectChanged, this, &Editor::proxiesChanged); // medios con proxy
    m_useProxies = QSettings().value(QStringLiteral("vista/proxies"), false).toBool();
    m_proxies.setAutoCreate(m_useProxies);
    // El estado de los proxies va aparte (proxyStates): no toca la lista de Medios.
    connect(&m_proxies, &ProxyManager::changed, this, &Editor::proxiesChanged);
    connect(&m_proxies, &ProxyManager::ready, this, [this](const QString&) {
        if (m_useProxies) {
            reloadVideo(); // un proxy nuevo: los clips de ese medio pasan a usarlo
        }
    });
    m_exportSettings =
        completeExportSettings(QSettings().value(QLatin1String(kExportSettingsKey)).toMap());
    refreshRecentProjects();
    // Una copia de recuperación de cada proyecto con cambios cada pocos minutos; las que
    // queden de una sesión que no se cerró bien se ofrecen en Proyectos.
    m_recoveryPool.setMaxThreadCount(1);
    m_autosaveTimer.setInterval(autosaveIntervalMs());
    connect(&m_autosaveTimer, &QTimer::timeout, this, &Editor::autosave);
    m_autosaveTimer.start();
    scanRecovery();
    connect(this, &Editor::selectionChanged, this, &Editor::selectedClipChanged);
    connect(this, &Editor::projectChanged, this, &Editor::selectedClipChanged);
}

Editor::~Editor() {
    // Al cerrar con normalidad, cada proyecto con cambios se guardó o se descartó; las
    // copias de lo que quede sin cambios ya no hacen falta.
    for (const auto& p : m_projects) {
        if (!p->history.modified()) {
            removeRecovery(p->recoveryKey);
        }
    }
    m_recoveryPool.waitForDone();
    shutdownTranscription();
    cancelAnchor();
    m_exporter.reset(); // cancela y espera
    m_clock.setAudio(nullptr, nullptr);
    m_audioWorker.reset();
    m_video.clear();
}

// --- Propiedades ------------------------------------------------------------------

void Editor::updateLibrary() {
    QVariantList media = mediaItems();
    QVariantList folderList = folders();
    QVariantList sequenceList = sequences();
    if (media == m_shownMedia && folderList == m_shownFolders && sequenceList == m_shownSequences) {
        return;
    }
    m_shownMedia = std::move(media);
    m_shownFolders = std::move(folderList);
    m_shownSequences = std::move(sequenceList);
    emit libraryChanged();
}

QVariantMap Editor::proxyStates() const {
    static const char* const kStates[] = {"", "queued", "working", "ready", "failed"};
    QVariantMap states;
    for (const MediaItem& item : document().media) {
        const QString path = QString::fromStdString(item.path);
        const ProxyManager::State state = m_proxies.state(path);
        if (state == ProxyManager::State::None) {
            continue;
        }
        states.insert(QString::number(item.id),
                      QVariantMap{{QStringLiteral("state"),
                                   QString::fromLatin1(kStates[static_cast<int>(state)])},
                                  {QStringLiteral("progress"), m_proxies.progress(path)}});
    }
    return states;
}

QVariantList Editor::mediaItems() const {
    QVariantList list;
    for (const MediaItem& item : document().media) {
        QVariantMap entry;
        entry[QStringLiteral("id")] = static_cast<qint64>(item.id);
        entry[QStringLiteral("name")] = QString::fromStdString(item.name);
        entry[QStringLiteral("path")] = QString::fromStdString(item.path);
        entry[QStringLiteral("duration")] = durationText(item.durationUs);
        entry[QStringLiteral("video")] = item.hasVideo;
        entry[QStringLiteral("audio")] = item.hasAudio;
        entry[QStringLiteral("folder")] = static_cast<qint64>(item.folder);
        entry[QStringLiteral("thumbnail")] =
            item.hasVideo ? QString(QStringLiteral("image://miniaturas/") +
                                    QString::fromLatin1(
                                        QUrl::toPercentEncoding(QString::fromStdString(item.path))))
                          : QString();
        entry[QStringLiteral("still")] = item.still;
        const std::optional<sharkcut::SolidColor> solid = sharkcut::solidColorOf(item.path);
        entry[QStringLiteral("solid")] =
            solid ? QStringLiteral("#%1").arg(solid->color, 6, 16, QLatin1Char('0')).toUpper()
                  : QString();
        entry[QStringLiteral("details")] =
            solid ? QStringLiteral("Color sólido · %1×%2").arg(item.width).arg(item.height)
            : item.still    ? QStringLiteral("Imagen · %1×%2").arg(item.width).arg(item.height)
            : item.hasVideo ? QStringLiteral("%1×%2 · %3 fps")
                                  .arg(item.width)
                                  .arg(item.height)
                                  .arg(fpsText(item.frameRate))
                            : QStringLiteral("Solo audio");
        list.append(entry);
    }
    return list;
}

QVariantMap Editor::clipEntry(std::size_t track, const Clip& clip) const {
    const Project& p = project();
    const MediaItem* item = p.findMedia(clip.media);
    QVariantMap entry;
    entry[QStringLiteral("id")] = static_cast<qint64>(clip.id);
    entry[QStringLiteral("link")] = static_cast<qint64>(clip.link);
    // Enlace entre una transición-clip y su sonido (no entre el video y el audio).
    const Clip* partner = clip.link != 0 ? p.clip(clip.link) : nullptr;
    entry[QStringLiteral("linkedEffect")] =
        partner != nullptr && (clip.isEffect() || partner->isEffect());
    entry[QStringLiteral("track")] = static_cast<int>(track);
    entry[QStringLiteral("video")] =
        track < p.tracks.size() && p.tracks[track].kind == sharkcut::TrackKind::Video;
    entry[QStringLiteral("media")] = static_cast<qint64>(clip.media);
    entry[QStringLiteral("start")] = static_cast<qint64>(clip.start);
    entry[QStringLiteral("length")] = static_cast<qint64>(clip.length);
    entry[QStringLiteral("in")] = static_cast<qint64>(clip.in);
    entry[QStringLiteral("volumeDb")] = clip.volumeDb;
    sharkcut::ClipTransform placement = clip.transform;
    placement.chromaKey = {}; // el icono indica solo cambios de colocación
    placement.effects = {};
    entry[QStringLiteral("transformed")] = placement != sharkcut::ClipTransform{};
    entry[QStringLiteral("name")] = item ? QString::fromStdString(item->name) : QStringLiteral("?");
    // Transición-clip: se dibuja como una transición, con su nombre.
    entry[QStringLiteral("effect")] = clip.isEffect();
    if (clip.isEffect()) {
        entry[QStringLiteral("name")] = fromView(sharkcut::transitionInfo(clip.effect->type).name);
    }
    // Texto libre: se dibuja con su color y con el texto (su primera línea) como nombre.
    entry[QStringLiteral("text")] = clip.isText();
    if (clip.isText()) {
        entry[QStringLiteral("name")] = textClipName(*clip.text);
    }
    // Keyframes (de cualquier propiedad) que caen dentro del clip, en la línea de tiempo.
    QVariantList keyframes;
    if (!clip.animation.empty()) {
        std::vector<std::int64_t> frames;
        for (const auto& keys : clip.animation.keys) {
            for (const sharkcut::Keyframe& k : keys) {
                const std::int64_t f = clip.start + (k.frame - clip.in);
                if (f >= clip.start && f < clip.end()) {
                    frames.push_back(f);
                }
            }
        }
        std::sort(frames.begin(), frames.end());
        frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
        for (const std::int64_t f : frames) {
            keyframes.append(static_cast<qint64>(f));
        }
    }
    entry[QStringLiteral("keyframes")] = keyframes;
    // Para las miniaturas de la línea de tiempo: el proxy si lo hay (mucho más rápido),
    // y los fps del medio (el tramo del clip va en fotogramas de la secuencia).
    if (item && item->hasVideo) {
        const QString original = QString::fromStdString(item->path);
        const QString proxy = m_proxies.readyPath(original);
        entry[QStringLiteral("thumbSource")] = proxy.isEmpty() ? original : proxy;
    }
    return entry;
}

qint64 Editor::snapFrame(qint64 frame, const QVariantList& exclude, double maxDistance,
                         bool withPlayhead) const {
    std::vector<ObjectId> skip;
    for (const QVariant& v : exclude) {
        skip.push_back(static_cast<ObjectId>(v.toLongLong()));
    }
    qint64 best = frame;
    double distance = maxDistance;
    auto test = [&](qint64 candidate) {
        const double d = std::abs(static_cast<double>(candidate - frame));
        if (d <= distance) {
            distance = d;
            best = candidate;
        }
    };
    test(0);
    if (withPlayhead) {
        test(m_currentFrame);
    }
    const Project& p = project();
    for (const sharkcut::Marker& marker : p.markers) {
        test(marker.frame);
    }
    const auto from =
        static_cast<std::int64_t>(std::floor(static_cast<double>(frame) - maxDistance));
    for (const sharkcut::Track& track : p.tracks) {
        // Solo los clips cerca de `frame` (cada pista está ordenada y sin solapes).
        auto it = std::upper_bound(track.clips.begin(), track.clips.end(), from,
                                   [](std::int64_t f, const Clip& c) { return f < c.end(); });
        for (; it != track.clips.end() &&
               static_cast<double>(it->start) <= static_cast<double>(frame) + maxDistance;
             ++it) {
            if (std::find(skip.begin(), skip.end(), it->id) != skip.end()) {
                continue;
            }
            test(it->start);
            test(it->end());
        }
    }
    return best;
}

QVariantMap Editor::nearestCut(int track, qint64 frame, double maxDistance) const {
    const Project& p = project();
    if (track < 0 || static_cast<std::size_t>(track) >= p.tracks.size()) {
        return {};
    }
    const auto& clips = p.tracks[static_cast<std::size_t>(track)].clips;
    std::optional<qint64> best;
    double distance = maxDistance;
    for (std::size_t i = 1; i < clips.size(); ++i) {
        if (clips[i - 1].end() != clips[i].start) {
            continue;
        }
        const double d = std::abs(static_cast<double>(clips[i].start - frame));
        if (d < distance) {
            distance = d;
            best = clips[i].start;
        }
    }
    if (!best) {
        return {};
    }
    return {{QStringLiteral("track"), track}, {QStringLiteral("cut"), *best}};
}

namespace {

// El clip que representa a un grupo de enlazados: la transición-clip si la hay (su
// sonido va con ella); si no, el primero.
const Clip* representative(const Project& p, const std::vector<ObjectId>& group) {
    for (ObjectId id : group) {
        const Clip* clip = p.clip(id);
        if (clip && clip->isEffect()) {
            return clip;
        }
    }
    return p.clip(group.front());
}

} // namespace

QVariantList Editor::clipGroup(qint64 clipId) const {
    const auto id = static_cast<ObjectId>(clipId);
    const Clip* clip = project().clip(id);
    if (!clip) {
        return {};
    }
    const bool inSelection =
        std::find(m_selection.begin(), m_selection.end(), id) != m_selection.end() ||
        (clip->link != 0 &&
         std::find(m_selection.begin(), m_selection.end(), clip->link) != m_selection.end());
    const std::vector<ObjectId> group =
        sharkcut::withLinked(project(), inSelection ? m_selection : std::vector<ObjectId>{id});
    QVariantList list;
    for (ObjectId member : group) {
        list.append(static_cast<qint64>(member));
    }
    return list;
}

QVariantMap Editor::groupRange(const QVariantList& clips) const {
    qint64 start = std::numeric_limits<qint64>::max();
    qint64 end = 0;
    for (const QVariant& v : clips) {
        if (const Clip* clip = project().clip(static_cast<ObjectId>(v.toLongLong()))) {
            start = std::min<qint64>(start, clip->start);
            end = std::max<qint64>(end, clip->end());
        }
    }
    if (end == 0) {
        start = 0;
    }
    return {{QStringLiteral("start"), start}, {QStringLiteral("end"), end}};
}

QVariantList Editor::tracks() const {
    QVariantList list;
    const Project& p = project();
    for (std::size_t t = 0; t < p.tracks.size(); ++t) {
        const sharkcut::Track& track = p.tracks[t];
        QVariantMap entry;
        entry[QStringLiteral("index")] = static_cast<int>(t);
        entry[QStringLiteral("name")] = QString::fromStdString(p.trackName(t));
        entry[QStringLiteral("video")] = track.kind == sharkcut::TrackKind::Video;
        entry[QStringLiteral("number")] = static_cast<int>(p.trackNumber(t));
        entry[QStringLiteral("hidden")] = track.hidden;
        entry[QStringLiteral("muted")] = track.muted;
        entry[QStringLiteral("locked")] = track.locked;
        entry[QStringLiteral("empty")] = track.clips.empty();
        list.append(entry);
    }
    return list;
}

QVariantMap Editor::selectedClip() const {
    const Project& p = project();
    const std::vector<ObjectId> group = sharkcut::withLinked(p, m_selection);
    if (group.empty()) {
        return {};
    }
    const Clip* first = representative(p, group);
    const bool single =
        group.size() == 1 || (group.size() == 2 && first->link != 0 &&
                              std::find(group.begin(), group.end(), first->link) != group.end());
    if (!single) {
        // Varios: cuántos de video, el primero (para aplicarles efectos) y si hay enlaces.
        int videos = 0;
        qint64 firstVideo = 0;
        bool linked = false;
        for (ObjectId id : group) {
            const auto ref = p.findClip(id);
            if (!ref) {
                continue;
            }
            linked = linked || p.tracks[ref->track].clips[ref->index].link != 0;
            if (p.tracks[ref->track].kind == sharkcut::TrackKind::Video && videos++ == 0) {
                firstVideo = static_cast<qint64>(id);
            }
        }
        return {{QStringLiteral("count"), static_cast<int>(group.size())},
                {QStringLiteral("videoCount"), videos},
                {QStringLiteral("firstVideo"), firstVideo},
                {QStringLiteral("linked"), linked}};
    }
    QVariantMap result;
    result[QStringLiteral("count")] = 1;
    if (first->isEffect()) {
        // Transición-clip: sus valores (sin transformación ni audio).
        const sharkcut::TransitionParams& e = *first->effect;
        const auto ref = p.findClip(first->id);
        result[QStringLiteral("video")] = qint64{0};
        result[QStringLiteral("audio")] = qint64{0};
        result[QStringLiteral("start")] = static_cast<qint64>(first->start);
        result[QStringLiteral("end")] = static_cast<qint64>(first->end());
        result[QStringLiteral("name")] = fromView(sharkcut::transitionInfo(e.type).name);
        result[QStringLiteral("effectClip")] = static_cast<qint64>(first->id);
        result[QStringLiteral("effect")] =
            QVariantMap{{QStringLiteral("typeIndex"), static_cast<int>(e.type)},
                        {QStringLiteral("intensity"), e.intensity},
                        {QStringLiteral("blur"), e.blur},
                        {QStringLiteral("easingIndex"), static_cast<int>(e.easing)},
                        {QStringLiteral("length"), static_cast<qint64>(e.durationFrames)},
                        {QStringLiteral("track"), QString::fromStdString(p.trackName(ref->track))},
                        {QStringLiteral("locked"), p.tracks[ref->track].locked}};
        // Enlazada con su efecto de sonido (se mueven y se quitan juntos).
        result[QStringLiteral("linked")] = group.size() == 2;
        return result;
    }
    result[QStringLiteral("linked")] = group.size() == 2;
    result[QStringLiteral("start")] = static_cast<qint64>(first->start);
    result[QStringLiteral("end")] = static_cast<qint64>(first->end());
    const MediaItem* item = p.findMedia(first->media);
    // Imagen fija (o texto): su duración se cambia desde Propiedades.
    result[QStringLiteral("still")] = (item && item->still) || first->isText();
    result[QStringLiteral("name")] =
        item ? QString::fromStdString(item->name) : QStringLiteral("?");
    if (first->isText()) {
        result[QStringLiteral("name")] = textClipName(*first->text);
        result[QStringLiteral("text")] = textInfo(*first);
    }
    result[QStringLiteral("video")] = qint64{0};
    result[QStringLiteral("audio")] = qint64{0};
    for (ObjectId id : group) {
        const auto ref = p.findClip(id);
        const sharkcut::Track& track = p.tracks[ref->track];
        const Clip& clip = track.clips[ref->index];
        const QString trackName = QString::fromStdString(p.trackName(ref->track));
        if (track.kind == sharkcut::TrackKind::Video) {
            const sharkcut::ClipTransform t = transformOf(id);
            const CanvasBox box = canvasBox(clip, t);
            result[QStringLiteral("video")] = static_cast<qint64>(id);
            result[QStringLiteral("videoTrack")] = trackName;
            result[QStringLiteral("videoLocked")] = track.locked;
            result[QStringLiteral("videoHidden")] = track.hidden;
            result[QStringLiteral("transform")] = transformToMap(t);
            result[QStringLiteral("animation")] = animationInfo(clip);
            result[QStringLiteral("box")] = QVariantMap{
                {QStringLiteral("left"), box.left},
                {QStringLiteral("top"), box.top},
                {QStringLiteral("width"), box.width},
                {QStringLiteral("height"), box.height},
                {QStringLiteral("fullWidth"), box.fullWidth},
                {QStringLiteral("fullHeight"), box.fullHeight},
                // Giro (grados, horario) alrededor del centro del video, en el lienzo.
                {QStringLiteral("rotation"), t.rotation},
                {QStringLiteral("pivotX"), project().settings.width / 2.0 + t.x},
                {QStringLiteral("pivotY"), project().settings.height / 2.0 + t.y},
            };
        } else {
            result[QStringLiteral("audio")] = static_cast<qint64>(id);
            result[QStringLiteral("audioTrack")] = trackName;
            result[QStringLiteral("audioLocked")] = track.locked;
            result[QStringLiteral("volumeDb")] = clip.volumeDb;
        }
    }
    return result;
}

std::shared_ptr<const sharkcut::Waveform> Editor::waveform(qint64 mediaId) {
    const MediaItem* item = project().findMedia(static_cast<ObjectId>(mediaId));
    if (!item || !item->hasAudio) {
        return nullptr;
    }
    return m_waveforms.get(QString::fromStdString(item->path));
}

bool Editor::waveformLoading(qint64 mediaId) const {
    const MediaItem* item = project().findMedia(static_cast<ObjectId>(mediaId));
    return item && m_waveforms.pending(QString::fromStdString(item->path));
}

QStringList Editor::mediaPaths() const {
    QStringList paths;
    for (const MediaItem& item : document().media) {
        if (item.hasVideo && !item.still) { // una imagen no necesita proxy
            paths << QString::fromStdString(item.path);
        }
    }
    return paths;
}

std::string Editor::decodePath(const MediaItem& item) const {
    if (m_useProxies) {
        const QString proxy = m_proxies.readyPath(QString::fromStdString(item.path));
        if (!proxy.isEmpty()) {
            return proxy.toStdString();
        }
    }
    return item.path;
}

void Editor::reloadVideo() {
    for (TrackVideo& track : m_video) {
        for (VideoSlot& slot : track.decoders) {
            slot.worker.reset();
            slot.media = 0;
            slot.path.clear();
            slot.clip = 0;
            slot.seekIndex = -1;
        }
    }
    const bool wasPlaying = m_playing;
    pause();
    seek(m_currentFrame);
    if (wasPlaying) {
        play();
    }
}

void Editor::setUseProxies(bool use) {
    if (use == m_useProxies) {
        return;
    }
    m_useProxies = use;
    QSettings().setValue(QStringLiteral("vista/proxies"), use);
    // Sin proxies, activarlos no cambiaría nada: mientras se usen, se crean los que falten.
    m_proxies.setAutoCreate(use);
    emit proxiesChanged();
    reloadVideo();
}

double Editor::stillSeconds() const {
    return QSettings().value(QStringLiteral("medios/duracionImagen"), 5.0).toDouble();
}

void Editor::setStillSeconds(double seconds) {
    seconds = std::clamp(seconds, 0.1, 3600.0);
    if (seconds == stillSeconds()) {
        return;
    }
    QSettings().setValue(QStringLiteral("medios/duracionImagen"), seconds);
    emit stillSecondsChanged();
}

QString Editor::proxyStatus() const {
    const int pending = m_proxies.pending();
    if (pending == 0) {
        return {};
    }
    return QStringLiteral("Creando proxies… %1 pendiente%2 · %3 %")
        .arg(pending)
        .arg(pending == 1 ? QString() : QStringLiteral("s"))
        .arg(std::lround(m_proxies.totalProgress() * 100));
}

void Editor::createProxies(const QVariantList& mediaIds) {
    QStringList paths;
    for (const MediaItem& item : document().media) {
        const bool wanted =
            mediaIds.isEmpty() ||
            std::any_of(mediaIds.begin(), mediaIds.end(), [&item](const QVariant& v) {
                return static_cast<ObjectId>(v.toLongLong()) == item.id;
            });
        if (wanted && item.hasVideo && !item.still) {
            paths << QString::fromStdString(item.path);
        }
    }
    m_proxies.create(paths);
}

void Editor::cancelProxies() {
    m_proxies.cancelAll();
}

void Editor::updateWaveformDirectory() {
    m_waveforms.setDirectory(
        current().path.isEmpty() ? QString() : current().path + QStringLiteral(".cache/ondas"));
    // Los proxies, en la misma caché (sin archivo de proyecto no hay dónde guardarlos).
    m_proxies.setDirectory(
        current().path.isEmpty() ? QString() : current().path + QStringLiteral(".cache/proxies"),
        mediaPaths());
}

sharkcut::ClipTransform Editor::transformOf(ObjectId clip) const {
    if (clip == m_previewClip && clip != 0) {
        return m_previewTransform;
    }
    const Clip* c = project().clip(clip);
    return c ? sharkcut::transformAt(*c, m_currentFrame) : sharkcut::ClipTransform{};
}

Editor::CanvasBox Editor::canvasBox(const Clip& clip, const sharkcut::ClipTransform& t) const {
    CanvasBox box;
    const MediaItem* item = project().findMedia(clip.media);
    const double canvasW = project().settings.width;
    const double canvasH = project().settings.height;
    if (clip.isText()) {
        // El fotograma de un texto es del tamaño del lienzo; la caja es la del texto.
        const QRectF rect =
            m_textFrames
                .get(textOf(clip), QSize(project().settings.width, project().settings.height))
                .rect;
        if (rect.isEmpty()) {
            return box;
        }
        box.fullWidth = rect.width() * t.scale;
        box.fullHeight = rect.height() * t.scale;
        const double left = canvasW / 2 + t.x + (rect.left() - canvasW / 2) * t.scale;
        const double top = canvasH / 2 + t.y + (rect.top() - canvasH / 2) * t.scale;
        // El recorte va sobre el lienzo entero: lo que quede del texto dentro.
        const double cropLeft = canvasW / 2 + t.x + (t.cropLeft - 0.5) * canvasW * t.scale;
        const double cropTop = canvasH / 2 + t.y + (t.cropTop - 0.5) * canvasH * t.scale;
        const double cropRight = canvasW / 2 + t.x + (0.5 - t.cropRight) * canvasW * t.scale;
        const double cropBottom = canvasH / 2 + t.y + (0.5 - t.cropBottom) * canvasH * t.scale;
        box.left = std::max(left, cropLeft);
        box.top = std::max(top, cropTop);
        box.width = std::max(0.0, std::min(left + box.fullWidth, cropRight) - box.left);
        box.height = std::max(0.0, std::min(top + box.fullHeight, cropBottom) - box.top);
        return box;
    }
    if (!item || item->width <= 0 || item->height <= 0) {
        return box;
    }
    // Ajuste al lienzo sin deformar (como el compositor) y después la escala.
    const double aspect = static_cast<double>(item->width) / item->height;
    double fitW = canvasW;
    double fitH = canvasW / aspect;
    if (aspect < canvasW / canvasH) {
        fitH = canvasH;
        fitW = canvasH * aspect;
    }
    box.fullWidth = fitW * t.scale;
    box.fullHeight = fitH * t.scale;
    const double left = canvasW / 2 + t.x - box.fullWidth / 2;
    const double top = canvasH / 2 + t.y - box.fullHeight / 2;
    box.left = left + t.cropLeft * box.fullWidth;
    box.top = top + t.cropTop * box.fullHeight;
    box.width = box.fullWidth * (1.0 - t.cropLeft - t.cropRight);
    box.height = box.fullHeight * (1.0 - t.cropTop - t.cropBottom);
    return box;
}

double Editor::fps() const {
    const sharkcut::FrameRate rate = project().settings.frameRate;
    return static_cast<double>(rate.num) / static_cast<double>(rate.den);
}

QString Editor::projectDescription() const {
    const sharkcut::ProjectSettings& s = project().settings;
    return QStringLiteral("%1×%2 · %3 fps").arg(s.width).arg(s.height).arg(fpsText(s.frameRate));
}

QString Editor::undoText() const {
    return canUndo() ? QStringLiteral("Deshacer: %1")
                           .arg(QString::fromStdString(current().history.undoLabel()))
                     : QStringLiteral("Deshacer");
}

QString Editor::redoText() const {
    return canRedo() ? QStringLiteral("Rehacer: %1")
                           .arg(QString::fromStdString(current().history.redoLabel()))
                     : QStringLiteral("Rehacer");
}

QVariantList Editor::selection() const {
    QVariantList list;
    for (ObjectId id : m_selection) {
        list.append(static_cast<qint64>(id));
    }
    return list;
}

QString Editor::selectionDescription() const {
    const Project& p = project();
    const std::vector<ObjectId> group = sharkcut::withLinked(p, m_selection);
    if (group.empty()) {
        return {};
    }
    const Clip* first = representative(p, group);
    const bool single =
        group.size() == 1 || (group.size() == 2 && first->link != 0 &&
                              std::find(group.begin(), group.end(), first->link) != group.end());
    if (!single) {
        return QStringLiteral("%1 clips").arg(group.size());
    }
    const MediaItem* item = p.findMedia(first->media);
    const auto tc = [this](std::int64_t frame) { return timecodeFor(frame); };
    const QString name =
        first->isEffect() ? QStringLiteral("Transición %1")
                                .arg(fromView(sharkcut::transitionInfo(first->effect->type).name))
        : first->isText() ? QStringLiteral("Texto «%1»").arg(textClipName(*first->text))
        : item            ? QString::fromStdString(item->name)
                          : QStringLiteral("?");
    return QStringLiteral("%1\n%2 → %3 · %4")
        .arg(name, tc(first->start), tc(first->end()), tc(first->length));
}

QString Editor::timecode() const {
    return timecodeFor(m_currentFrame);
}

QString Editor::durationTimecode() const {
    return timecodeFor(frameCount());
}

QString Editor::timecodeFor(qint64 frame) const {
    return QString::fromStdString(sharkcut::formatTimecode(frame, project().settings.frameRate));
}

void Editor::setMuted(bool muted) {
    if (m_muted == muted) {
        return;
    }
    m_muted = muted;
    if (m_audioOutput) {
        m_audioOutput->setVolume(muted ? 0.0f : 1.0f);
    }
    emit mutedChanged();
}

QSize Editor::canvasSize() const {
    return {project().settings.width, project().settings.height};
}

sharkcut::CompositorInput Editor::composition() const {
    sharkcut::CompositorInput input;
    const QSize canvas(project().settings.width, project().settings.height);
    for (const Project::Layer& layer : project().videoLayersAt(m_currentFrame)) {
        if (layer.clip->isText()) {
            // Texto libre: un fotograma del tamaño del lienzo, sin decodificar nada.
            const auto text = m_textFrames.get(textOf(*layer.clip), canvas);
            if (text.frame) {
                sharkcut::CompositorLayer& out = input.layers.emplace_back();
                out.a = text.frame;
                out.transform = transformOf(layer.clip->id);
            }
            continue;
        }
        if (layer.clip->isEffect()) {
            // Transición-clip: transforma todo lo de debajo.
            sharkcut::CompositorLayer& out = input.layers.emplace_back();
            out.adjust = true;
            out.transition = layer.clip->effect;
            out.state = sharkcut::evaluateTransition(*layer.clip->effect, layer.effectFrame);
            continue;
        }
        if (layer.track >= m_video.size()) {
            continue;
        }
        const TrackVideo& track = m_video[layer.track];
        auto a = track.frameOf(layer.clip->id);
        auto b = layer.transition ? track.frameOf(layer.clipB->id) : nullptr;
        if (!a && !b) {
            a = track.last; // mientras llega el fotograma de este clip
        }
        if (!a && !b) {
            continue;
        }
        sharkcut::CompositorLayer& out = input.layers.emplace_back();
        if (layer.transition && a && b) {
            out.a = a;
            out.b = b;
            out.transform = transformOf(layer.clip->id);
            out.transformB = transformOf(layer.clipB->id);
            out.transition = layer.transition->params;
            out.state =
                sharkcut::evaluateTransition(layer.transition->params, layer.transitionFrame);
        } else {
            // Solo ha llegado uno de los dos: se muestra el que corresponde a ese lado
            // del corte, como si no hubiera transición.
            const bool showB =
                !a || (b && layer.transition && m_currentFrame >= layer.clipB->start);
            out.a = showB ? b : a;
            out.transform = transformOf(showB ? layer.clipB->id : layer.clip->id);
        }
    }
    sharkcut::CaptionImageCache::StyleOverride preview;
    if (m_captionPreview) {
        preview = [this](std::size_t track, const sharkcut::Caption& c) {
            const CaptionPreview& pv = *m_captionPreview;
            const bool affected =
                std::find(pv.tracks.begin(), pv.tracks.end(), track) != pv.tracks.end() ||
                std::find(pv.captions.begin(), pv.captions.end(), c.id) != pv.captions.end();
            return affected ? &pv.style : nullptr;
        };
    }
    input.overlays = m_captionImages.overlaysAt(project(), m_currentFrame, canvas, preview);
    return input;
}

// --- Proyectos abiertos y pestañas ------------------------------------------------

Editor::OpenProject& Editor::current() {
    return *findProject(m_activeProject);
}

const Editor::OpenProject& Editor::current() const {
    return *findProject(m_activeProject);
}

Editor::OpenProject* Editor::findProject(quint64 uid) {
    for (auto& p : m_projects) {
        if (p->uid == uid) {
            return p.get();
        }
    }
    return nullptr;
}

const Editor::OpenProject* Editor::findProject(quint64 uid) const {
    for (const auto& p : m_projects) {
        if (p->uid == uid) {
            return p.get();
        }
    }
    return nullptr;
}

QString Editor::nameOf(const OpenProject& project) const {
    if (!project.path.isEmpty()) {
        return QFileInfo(project.path).completeBaseName();
    }
    return project.newName.isEmpty() ? QStringLiteral("Sin título") : project.newName;
}

Editor::OpenProject& Editor::addProject(sharkcut::Document document, QString path,
                                        QString newName) {
    auto project = std::make_unique<OpenProject>();
    project->uid = m_nextUid++;
    project->history.reset(std::move(document));
    project->path = std::move(path);
    project->newName = std::move(newName);
    m_projects.push_back(std::move(project));
    return *m_projects.back();
}

ObjectId Editor::activeSequenceId() const {
    if (m_activeTab < 0 || m_activeTab >= static_cast<int>(m_tabs.size())) {
        return 0;
    }
    const Tab& tab = m_tabs[static_cast<std::size_t>(m_activeTab)];
    if (tab.project != m_activeProject || !document().sequence(tab.sequence)) {
        return 0;
    }
    return tab.sequence;
}

void Editor::refreshView() {
    const ObjectId sequence = activeSequenceId();
    m_view = sequence != 0 ? document().view(sequence) : Project{};
}

sharkcut::Workspace Editor::workspaceOf(const OpenProject& project) const {
    sharkcut::Workspace workspace;
    for (std::size_t i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].project == project.uid) {
            workspace.openSequences.push_back(m_tabs[i].sequence);
            if (static_cast<int>(i) == m_activeTab) {
                workspace.activeSequence = m_tabs[i].sequence;
            }
        }
    }
    return workspace;
}

void Editor::switchTo(int tab) {
    pause();
    if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size())) {
        m_tabs[static_cast<std::size_t>(m_activeTab)].frame = m_currentFrame;
    }
    m_activeTab = tab >= 0 && tab < static_cast<int>(m_tabs.size()) ? tab : -1;
    qint64 frame = 0;
    if (m_activeTab >= 0) {
        const Tab& t = m_tabs[static_cast<std::size_t>(m_activeTab)];
        m_activeProject = t.project;
        frame = t.frame;
    }
    m_selection.clear();
    m_selectedTransition = 0;
    m_previewClip = 0;
    updateWaveformDirectory();
    setCurrentFrame(frame);
    onProjectChanged();
    emit projectsChanged();
    emit fileChanged();
}

QVariantList Editor::projects() const {
    QVariantList list;
    for (const auto& p : m_projects) {
        QVariantMap entry;
        entry[QStringLiteral("uid")] = static_cast<qint64>(p->uid);
        entry[QStringLiteral("name")] = nameOf(*p);
        entry[QStringLiteral("modified")] = p->history.modified();
        entry[QStringLiteral("active")] = p->uid == m_activeProject;
        entry[QStringLiteral("saved")] = !p->path.isEmpty();
        list.append(entry);
    }
    return list;
}

QVariantList Editor::tabs() const {
    QVariantList list;
    for (std::size_t i = 0; i < m_tabs.size(); ++i) {
        const Tab& tab = m_tabs[i];
        const OpenProject* project = findProject(tab.project);
        const sharkcut::Sequence* sequence =
            project ? project->history.state().sequence(tab.sequence) : nullptr;
        if (!sequence) {
            continue;
        }
        QVariantMap entry;
        entry[QStringLiteral("index")] = static_cast<int>(i);
        entry[QStringLiteral("project")] = static_cast<qint64>(tab.project);
        entry[QStringLiteral("sequence")] = static_cast<qint64>(tab.sequence);
        entry[QStringLiteral("name")] = QString::fromStdString(sequence->name);
        entry[QStringLiteral("projectName")] = nameOf(*project);
        entry[QStringLiteral("active")] = static_cast<int>(i) == m_activeTab;
        entry[QStringLiteral("canvas")] =
            aspectLabel(sequence->settings.width, sequence->settings.height);
        list.append(entry);
    }
    return list;
}

QString Editor::sequenceName() const {
    const sharkcut::Sequence* sequence = document().sequence(activeSequenceId());
    return sequence ? QString::fromStdString(sequence->name) : QString();
}

QVariantList Editor::folders() const {
    QVariantList list;
    const sharkcut::Document& d = document();
    for (const sharkcut::Folder& folder : d.folders) {
        const auto count =
            std::count_if(d.media.begin(), d.media.end(),
                          [&folder](const MediaItem& m) { return m.folder == folder.id; }) +
            std::count_if(d.sequences.begin(), d.sequences.end(),
                          [&folder](const sharkcut::Sequence& s) { return s.folder == folder.id; });
        QVariantMap entry;
        entry[QStringLiteral("id")] = static_cast<qint64>(folder.id);
        entry[QStringLiteral("name")] = QString::fromStdString(folder.name);
        entry[QStringLiteral("count")] = static_cast<int>(count);
        list.append(entry);
    }
    return list;
}

QVariantList Editor::sequences() const {
    QVariantList list;
    const sharkcut::Document& d = document();
    for (const sharkcut::Sequence& sequence : d.sequences) {
        const Project view = d.view(sequence.id);
        const sharkcut::ProjectSettings& s = sequence.settings;
        QVariantMap entry;
        entry[QStringLiteral("id")] = static_cast<qint64>(sequence.id);
        entry[QStringLiteral("name")] = QString::fromStdString(sequence.name);
        entry[QStringLiteral("folder")] = static_cast<qint64>(sequence.folder);
        entry[QStringLiteral("canvas")] = aspectLabel(s.width, s.height);
        entry[QStringLiteral("details")] =
            QStringLiteral("%1×%2 · %3 fps").arg(s.width).arg(s.height).arg(fpsText(s.frameRate));
        entry[QStringLiteral("duration")] =
            QString::fromStdString(sharkcut::formatTimecode(view.length(), s.frameRate));
        entry[QStringLiteral("width")] = s.width;
        entry[QStringLiteral("height")] = s.height;
        entry[QStringLiteral("fps")] =
            static_cast<double>(s.frameRate.num) / static_cast<double>(s.frameRate.den);
        entry[QStringLiteral("frames")] = static_cast<qint64>(view.length());
        entry[QStringLiteral("active")] = sequence.id == activeSequenceId();
        entry[QStringLiteral("open")] =
            std::any_of(m_tabs.begin(), m_tabs.end(), [&](const Tab& t) {
                return t.project == m_activeProject && t.sequence == sequence.id;
            });
        list.append(entry);
    }
    return list;
}

void Editor::activateProject(qint64 uid) {
    const auto id = static_cast<quint64>(uid);
    if (!findProject(id) || id == m_activeProject) {
        return;
    }
    // Su última pestaña, si tiene alguna abierta.
    int tab = -1;
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        if (m_tabs[static_cast<std::size_t>(i)].project == id) {
            tab = i;
        }
    }
    if (tab >= 0) {
        switchTo(tab);
        return;
    }
    pause();
    if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size())) {
        m_tabs[static_cast<std::size_t>(m_activeTab)].frame = m_currentFrame;
    }
    m_activeProject = id;
    m_activeTab = -1;
    switchTo(-1);
}

void Editor::closeProject(qint64 uid) {
    const auto id = static_cast<quint64>(uid);
    if (!findProject(id) || m_fileBusy) {
        return;
    }
    pause();
    // La pestaña activa, para volver a encontrarla después de quitar las del proyecto.
    std::optional<Tab> active;
    if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size())) {
        m_tabs[static_cast<std::size_t>(m_activeTab)].frame = m_currentFrame;
        active = m_tabs[static_cast<std::size_t>(m_activeTab)];
    }
    std::erase_if(m_tabs, [id](const Tab& t) { return t.project == id; });
    removeRecovery(findProject(id)->recoveryKey); // se cerró a propósito
    std::erase_if(m_projects, [id](const auto& p) { return p->uid == id; });
    if (m_projects.empty()) {
        OpenProject& fresh = addProject(newDocument("Secuencia 1", 0, 0, 0), {}, {});
        m_tabs.push_back({fresh.uid, fresh.history.state().sequences.front().id, 0});
    }
    int tab = -1;
    if (id != m_activeProject && findProject(m_activeProject)) {
        for (int i = 0; active && i < static_cast<int>(m_tabs.size()); ++i) {
            const Tab& t = m_tabs[static_cast<std::size_t>(i)];
            if (t.project == active->project && t.sequence == active->sequence) {
                tab = i;
            }
        }
    } else {
        // Otro proyecto: el último abierto, en su última pestaña.
        m_activeProject = m_projects.back()->uid;
        for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
            if (m_tabs[static_cast<std::size_t>(i)].project == m_activeProject) {
                tab = i;
            }
        }
    }
    m_activeTab = -1; // la posición del cabezal ya está guardada
    switchTo(tab);
}

bool Editor::projectModified(qint64 uid) const {
    const OpenProject* project = findProject(static_cast<quint64>(uid));
    return project && project->history.modified();
}

QVariantList Editor::unsavedProjects() const {
    QVariantList list;
    for (const auto& p : m_projects) {
        if (p->history.modified()) {
            list.append(static_cast<qint64>(p->uid));
        }
    }
    return list;
}

void Editor::openSequence(qint64 sequenceId) {
    const auto id = static_cast<ObjectId>(sequenceId);
    if (!document().sequence(id)) {
        return;
    }
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        const Tab& tab = m_tabs[static_cast<std::size_t>(i)];
        if (tab.project == m_activeProject && tab.sequence == id) {
            switchTo(i);
            return;
        }
    }
    m_tabs.push_back({m_activeProject, id, 0});
    switchTo(static_cast<int>(m_tabs.size()) - 1);
}

void Editor::activateTab(int index) {
    if (index >= 0 && index < static_cast<int>(m_tabs.size()) && index != m_activeTab) {
        switchTo(index);
    }
}

void Editor::closeTab(int index) {
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) {
        return;
    }
    const quint64 project = m_tabs[static_cast<std::size_t>(index)].project;
    const bool wasActive = index == m_activeTab;
    if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size())) {
        m_tabs[static_cast<std::size_t>(m_activeTab)].frame = m_currentFrame;
    }
    m_tabs.erase(m_tabs.begin() + index);
    int next = m_activeTab;
    if (wasActive) {
        // Otra pestaña del mismo proyecto, si queda alguna (la más cercana).
        next = -1;
        for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
            if (m_tabs[static_cast<std::size_t>(i)].project == project) {
                next = i;
                if (i >= index) {
                    break;
                }
            }
        }
        m_activeProject = project;
    } else if (index < m_activeTab) {
        --next;
    }
    m_activeTab = -1;
    switchTo(next);
}

void Editor::newSequence(const QString& name, int width, int height, int fps) {
    ObjectId created = 0;
    sharkcut::ProjectSettings settings = project().settings;
    if (width > 0 && height > 0) {
        settings.width = width;
        settings.height = height;
        settings.adoptCanvas = false;
    }
    if (fps > 0) {
        settings.frameRate = {fps, 1};
        settings.adoptFrameRate = false;
    }
    const std::string sequenceName =
        name.trimmed().isEmpty()
            ? QStringLiteral("Secuencia %1").arg(document().sequences.size() + 1).toStdString()
            : name.trimmed().toStdString();
    editDocument(QStringLiteral("Nueva secuencia"), [&](sharkcut::Document& d) {
        created = sharkcut::addSequence(d, sequenceName, settings);
        return created != 0;
    });
    if (created != 0) {
        openSequence(static_cast<qint64>(created));
    }
}

void Editor::renameSequence(qint64 sequenceId, const QString& name) {
    const std::string text = name.trimmed().toStdString();
    editDocument(QStringLiteral("Renombrar secuencia"), [&](sharkcut::Document& d) {
        return sharkcut::renameSequence(d, static_cast<ObjectId>(sequenceId), text);
    });
}

void Editor::duplicateSequence(qint64 sequenceId, bool vertical) {
    const sharkcut::Sequence* original = document().sequence(static_cast<ObjectId>(sequenceId));
    if (!original) {
        return;
    }
    const std::string name =
        original->name + (vertical ? std::string(" (vertical)") : std::string(" (copia)"));
    std::optional<std::pair<int, int>> canvas;
    if (vertical) {
        // El lado corto del lienzo original pasa a ser el ancho (1920×1080 → 1080×1920).
        const int shortSide = std::min(original->settings.width, original->settings.height);
        canvas = std::pair{shortSide, shortSide * 16 / 9};
    }
    ObjectId created = 0;
    editDocument(
        vertical ? QStringLiteral("Duplicar como vertical") : QStringLiteral("Duplicar secuencia"),
        [&](sharkcut::Document& d) {
            created =
                sharkcut::duplicateSequence(d, static_cast<ObjectId>(sequenceId), name, canvas);
            return created != 0;
        });
    if (created != 0) {
        openSequence(static_cast<qint64>(created));
    }
}

void Editor::removeSequence(qint64 sequenceId) {
    const auto id = static_cast<ObjectId>(sequenceId);
    if (!editDocument(QStringLiteral("Quitar secuencia"),
                      [id](sharkcut::Document& d) { return sharkcut::removeSequence(d, id); })) {
        setError(QStringLiteral("Un proyecto necesita al menos una secuencia"));
        return;
    }
    // Sus pestañas se cierran (quitarla se puede deshacer; la pestaña se vuelve a abrir
    // desde Medios).
    pruneTabs();
    emit projectsChanged();
}

void Editor::newFolder(const QString& name) {
    const std::string text =
        name.trimmed().isEmpty() ? std::string("Carpeta nueva") : name.trimmed().toStdString();
    editDocument(QStringLiteral("Nueva carpeta"),
                 [&text](sharkcut::Document& d) { return sharkcut::addFolder(d, text) != 0; });
}

namespace {

// Nombre de un color sólido en la biblioteca (sigue al color mientras no se cambie a mano).
std::string solidColorName(std::uint32_t color) {
    return (QStringLiteral("Color #") +
            QStringLiteral("%1").arg(color, 6, 16, QLatin1Char('0')).toUpper())
        .toStdString();
}

std::optional<std::uint32_t> colorFromText(const QString& text) {
    const QColor color(text);
    if (!color.isValid()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(color.rgb()) & 0xFFFFFFu;
}

} // namespace

qint64 Editor::addSolidColor(const QString& color, qint64 folderId) {
    const std::optional<std::uint32_t> rgb = colorFromText(color);
    if (!rgb || !hasSequence()) {
        return 0;
    }
    // Del tamaño del lienzo de la secuencia activa, así la llena.
    const sharkcut::ProjectSettings& settings = project().settings;
    const sharkcut::SolidColor solid{*rgb, settings.width, settings.height};
    const auto durationUs = static_cast<std::int64_t>(std::llround(stillSeconds() * 1e6));
    ObjectId created = 0;
    editDocument(QStringLiteral("Nuevo color sólido"), [&](sharkcut::Document& d) {
        created = sharkcut::addSolidColor(d, solid, solidColorName(*rgb), durationUs,
                                          settings.frameRate, static_cast<ObjectId>(folderId));
        return created != 0;
    });
    return static_cast<qint64>(created);
}

void Editor::setSolidColor(qint64 mediaId, const QString& color) {
    const std::optional<std::uint32_t> rgb = colorFromText(color);
    if (!rgb) {
        return;
    }
    const auto id = static_cast<ObjectId>(mediaId);
    editDocument(QStringLiteral("Cambiar el color sólido"), [&](sharkcut::Document& d) {
        for (MediaItem& item : d.media) {
            const std::optional<sharkcut::SolidColor> old = sharkcut::solidColorOf(item.path);
            if (item.id == id && old && item.name == solidColorName(old->color)) {
                item.name = solidColorName(*rgb);
            }
        }
        return sharkcut::setSolidColor(d, id, *rgb);
    });
}

void Editor::renameFolder(qint64 folderId, const QString& name) {
    const std::string text = name.trimmed().toStdString();
    editDocument(QStringLiteral("Renombrar carpeta"), [&](sharkcut::Document& d) {
        return sharkcut::renameFolder(d, static_cast<ObjectId>(folderId), text);
    });
}

void Editor::removeFolder(qint64 folderId) {
    editDocument(QStringLiteral("Quitar carpeta"), [folderId](sharkcut::Document& d) {
        return sharkcut::removeFolder(d, static_cast<ObjectId>(folderId));
    });
}

void Editor::moveToFolder(const QVariantList& items, qint64 folderId) {
    std::vector<ObjectId> ids;
    for (const QVariant& item : items) {
        ids.push_back(static_cast<ObjectId>(item.toLongLong()));
    }
    editDocument(QStringLiteral("Mover a la carpeta"), [&](sharkcut::Document& d) {
        return sharkcut::moveToFolder(d, ids, static_cast<ObjectId>(folderId));
    });
}

// --- Archivo del proyecto -------------------------------------------------------------

QString Editor::projectName() const {
    return nameOf(current());
}

QString Editor::canvasLabel() const {
    return aspectLabel(project().settings.width, project().settings.height);
}

QUrl Editor::defaultExportFolder() const {
    if (!current().path.isEmpty()) {
        return QUrl::fromLocalFile(QFileInfo(current().path).absolutePath());
    }
    return QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));
}

void Editor::setFileBusy(bool busy) {
    if (m_fileBusy != busy) {
        m_fileBusy = busy;
        emit fileBusyChanged();
    }
}

void Editor::newProject(const QString& name, int width, int height, int fps) {
    if (m_fileBusy) {
        return;
    }
    const QString trimmed = name.trimmed();
    OpenProject& project = addProject(
        newDocument(trimmed.isEmpty() ? std::string("Secuencia 1") : trimmed.toStdString(), width,
                    height, fps),
        {}, trimmed);
    m_tabs.push_back({project.uid, project.history.state().sequences.front().id, 0});
    setError({});
    switchTo(static_cast<int>(m_tabs.size()) - 1);
}

void Editor::openProject(const QUrl& file) {
    openProjectPath(file.isLocalFile() ? file.toLocalFile() : file.toString());
}

namespace {

// Tras leer un proyecto (en segundo plano): los efectos de sonido van con SharkCut, así
// que si se instaló en otra carpeta (u otro equipo) se buscan en la de ahora. Devuelve
// los medios que no se encuentran.
QStringList relinkAndCheck(sharkcut::LoadedDocument& loaded) {
    for (MediaItem& item : loaded.document.media) {
        if (sharkcut::isSolidColorPath(item.path)) {
            continue; // un color sólido no tiene archivo
        }
        std::error_code ignored;
        const std::filesystem::path current = sharkcut::pathFromUtf8(item.path);
        if (std::filesystem::exists(current, ignored)) {
            continue;
        }
        const QString file = QString::fromStdString(sharkcut::utf8FromPath(current.filename()));
        const bool isEffect = std::any_of(
            sharkcut::kSoundEffects.begin(), sharkcut::kSoundEffects.end(),
            [&file](const sharkcut::SoundEffect& e) { return fromView(e.file) == file; });
        const QString candidate = QDir(soundFolder()).filePath(file);
        if (isEffect && QFileInfo::exists(candidate)) {
            item.path = candidate.toStdString();
        }
    }
    QStringList missing;
    for (const MediaItem& item : loaded.document.media) {
        std::error_code ignored;
        if (!sharkcut::isSolidColorPath(item.path) &&
            !std::filesystem::exists(sharkcut::pathFromUtf8(item.path), ignored)) {
            missing << QString::fromStdString(item.name);
        }
    }
    return missing;
}

} // namespace

void Editor::openProjectPath(const QString& path) {
    if (path.isEmpty() || m_fileBusy) {
        return;
    }
    // Si ya está abierto, solo se activa.
    const QString wanted = QFileInfo(path).absoluteFilePath();
    for (const auto& p : m_projects) {
        if (!p->path.isEmpty() && QFileInfo(p->path).absoluteFilePath() == wanted) {
            activateProject(static_cast<qint64>(p->uid));
            emit opened(true);
            return;
        }
    }
    pause();
    setFileBusy(true);
    QPointer<Editor> self(this);
    QThreadPool::globalInstance()->start([self, path] {
        auto loaded = std::make_shared<sharkcut::LoadedDocument>();
        QString error;
        QStringList missing;
        try {
            *loaded = sharkcut::loadDocument(sharkcut::pathFromUtf8(path.toStdString()));
            missing = relinkAndCheck(*loaded);
        } catch (const std::exception& e) {
            error = QString::fromStdString(e.what());
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, loaded, path, error, missing] {
                if (self) {
                    self->finishOpen(loaded, path, error, missing);
                }
            },
            Qt::QueuedConnection);
    });
}

void Editor::finishOpen(const std::shared_ptr<sharkcut::LoadedDocument>& loaded,
                        const QString& path, const QString& error, const QStringList& missing,
                        const QString& recoveryKey, const QString& recoveredName) {
    setFileBusy(false);
    if (!error.isEmpty()) {
        setError(QStringLiteral("No se pudo abrir el proyecto: %1").arg(error));
        emit opened(false);
        return;
    }
    // Un proyecto nuevo que no se llegó a usar se sustituye por el que se abre.
    if (m_projects.size() == 1) {
        const OpenProject& only = *m_projects.front();
        const sharkcut::Document& d = only.history.state();
        const bool untouched = only.path.isEmpty() && !only.history.modified() &&
                               !only.history.canUndo() && d.media.empty();
        if (untouched) {
            const quint64 uid = only.uid;
            std::erase_if(m_tabs, [uid](const Tab& t) { return t.project == uid; });
            m_projects.clear();
            m_activeTab = -1;
        }
    }
    OpenProject& project = addProject(std::move(loaded->document), path, recoveredName);
    if (!recoveryKey.isEmpty()) {
        // Recuperado: tiene cambios que no están en su archivo (sigue usando la misma
        // copia de recuperación hasta que se guarde).
        project.history.markSaved(nullptr);
        project.recoveryKey = recoveryKey;
        m_recoverable.erase(std::remove_if(m_recoverable.begin(), m_recoverable.end(),
                                           [&recoveryKey](const QVariant& v) {
                                               return v.toMap().value(QStringLiteral("key")) ==
                                                      recoveryKey;
                                           }),
                            m_recoverable.end());
        emit recoveryChanged();
    }
    int active = -1;
    for (ObjectId sequence : loaded->workspace.openSequences) {
        m_tabs.push_back({project.uid, sequence, 0});
        if (sequence == loaded->workspace.activeSequence) {
            active = static_cast<int>(m_tabs.size()) - 1;
        }
    }
    setError(missing.isEmpty() ? QString()
                               : QStringLiteral("No se encuentran estos medios: %1")
                                     .arg(missing.join(QStringLiteral(", "))));
    if (m_activeTab >= 0) {
        m_tabs[static_cast<std::size_t>(m_activeTab)].frame = m_currentFrame;
    }
    m_activeTab = -1;
    m_activeProject = project.uid;
    switchTo(active);
    if (!path.isEmpty()) {
        addRecentProject(path);
    }
    emit opened(true);
}

// --- Autoguardado y recuperación -----------------------------------------------------

QString Editor::recoveryDirectory() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("recuperacion"));
}

void Editor::autosave() {
    const QString directory = recoveryDirectory();
    for (const auto& p : m_projects) {
        OpenProject& project = *p;
        const std::shared_ptr<const sharkcut::Document> snapshot = project.history.snapshot();
        if (!project.history.modified() || snapshot == project.autosaved) {
            continue;
        }
        if (project.recoveryKey.isEmpty()) {
            project.recoveryKey = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
        project.autosaved = snapshot;
        const sharkcut::Workspace workspace = workspaceOf(project);
        const QString key = project.recoveryKey;
        const QString path = project.path;
        const QString name = nameOf(project);
        // Las rutas relativas de los medios se calculan respecto al archivo del proyecto
        // (o a la copia, si aún no tiene): así se leen igual al recuperarlo.
        m_recoveryPool.start([directory, key, path, name, snapshot, workspace] {
            QDir().mkpath(directory);
            const QString file = QDir(directory).filePath(key + QStringLiteral(".sharkp"));
            const QString reference = path.isEmpty() ? file : path;
            std::string text;
            try {
                text = sharkcut::documentToJson(
                    *snapshot, sharkcut::pathFromUtf8(reference.toStdString()), workspace);
            } catch (const std::exception&) {
                return; // texto no válido: ya avisará el guardado normal
            }
            QSaveFile out(file);
            if (!out.open(QIODevice::WriteOnly)) {
                return;
            }
            out.write(text.data(), static_cast<qint64>(text.size()));
            if (!out.commit()) {
                return;
            }
            const QJsonObject meta{
                {QStringLiteral("path"), path},
                {QStringLiteral("name"), name},
                {QStringLiteral("date"), QDateTime::currentDateTime().toString(Qt::ISODate)},
            };
            QSaveFile metaFile(QDir(directory).filePath(key + QStringLiteral(".json")));
            if (metaFile.open(QIODevice::WriteOnly)) {
                metaFile.write(QJsonDocument(meta).toJson());
                metaFile.commit();
            }
        });
    }
}

void Editor::removeRecovery(const QString& key) {
    if (key.isEmpty()) {
        return;
    }
    const QString directory = recoveryDirectory();
    // En la misma cola que las escrituras: nunca se adelanta a una copia pendiente.
    m_recoveryPool.start([directory, key] {
        QFile::remove(QDir(directory).filePath(key + QStringLiteral(".sharkp")));
        QFile::remove(QDir(directory).filePath(key + QStringLiteral(".json")));
    });
}

void Editor::scanRecovery() {
    const QString directory = recoveryDirectory();
    QPointer<Editor> self(this);
    QThreadPool::globalInstance()->start([self, directory] {
        QVariantList list;
        const QDir dir(directory);
        for (const QFileInfo& info :
             dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Time)) {
            QFile file(info.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }
            const QJsonObject meta = QJsonDocument::fromJson(file.readAll()).object();
            const QString key = info.completeBaseName();
            if (!QFileInfo::exists(dir.filePath(key + QStringLiteral(".sharkp")))) {
                continue;
            }
            const QDateTime date =
                QDateTime::fromString(meta.value(QStringLiteral("date")).toString(), Qt::ISODate);
            QVariantMap entry;
            entry[QStringLiteral("key")] = key;
            entry[QStringLiteral("name")] = meta.value(QStringLiteral("name")).toString();
            entry[QStringLiteral("path")] = meta.value(QStringLiteral("path")).toString();
            entry[QStringLiteral("date")] =
                QLocale().toString(date, QStringLiteral("d MMM yyyy, HH:mm"));
            list.append(entry);
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, list] {
                if (!self) {
                    return;
                }
                // Sin las de los proyectos que ya están abiertos en esta sesión.
                QVariantList pending;
                for (const QVariant& v : list) {
                    const QString key = v.toMap().value(QStringLiteral("key")).toString();
                    if (std::none_of(self->m_projects.begin(), self->m_projects.end(),
                                     [&key](const auto& p) { return p->recoveryKey == key; })) {
                        pending.append(v);
                    }
                }
                self->m_recoverable = pending;
                emit self->recoveryChanged();
            },
            Qt::QueuedConnection);
    });
}

void Editor::recoverProject(const QString& key) {
    if (m_fileBusy) {
        return;
    }
    QVariantMap meta;
    for (const QVariant& v : m_recoverable) {
        if (v.toMap().value(QStringLiteral("key")) == key) {
            meta = v.toMap();
        }
    }
    if (meta.isEmpty()) {
        return;
    }
    pause();
    setFileBusy(true);
    const QString file = QDir(recoveryDirectory()).filePath(key + QStringLiteral(".sharkp"));
    const QString path = meta.value(QStringLiteral("path")).toString();
    const QString name = meta.value(QStringLiteral("name")).toString();
    QPointer<Editor> self(this);
    QThreadPool::globalInstance()->start([self, file, path, name, key] {
        auto loaded = std::make_shared<sharkcut::LoadedDocument>();
        QString error;
        QStringList missing;
        try {
            QFile in(file);
            if (!in.open(QIODevice::ReadOnly)) {
                throw std::runtime_error("no se puede leer la copia de recuperación");
            }
            const QByteArray text = in.readAll();
            const QString reference = path.isEmpty() ? file : path;
            *loaded = sharkcut::documentFromJson(
                std::string_view(text.constData(), static_cast<std::size_t>(text.size())),
                sharkcut::pathFromUtf8(reference.toStdString()));
            missing = relinkAndCheck(*loaded);
        } catch (const std::exception& e) {
            error = QString::fromStdString(e.what());
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, loaded, path, error, missing, key, name] {
                if (self) {
                    self->finishOpen(loaded, path, error, missing, key,
                                     path.isEmpty() ? name : QString());
                }
            },
            Qt::QueuedConnection);
    });
}

void Editor::discardRecovery(const QString& key) {
    removeRecovery(key);
    m_recoverable.erase(std::remove_if(m_recoverable.begin(), m_recoverable.end(),
                                       [&key](const QVariant& v) {
                                           return v.toMap().value(QStringLiteral("key")) == key;
                                       }),
                        m_recoverable.end());
    emit recoveryChanged();
}

bool Editor::save() {
    if (current().path.isEmpty()) {
        return false;
    }
    saveToPath(m_activeProject, current().path);
    return true;
}

void Editor::saveAs(const QUrl& file) {
    QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (path.isEmpty()) {
        return;
    }
    const QString extension =
        QString::fromUtf8(sharkcut::kProjectExtension.data(),
                          static_cast<qsizetype>(sharkcut::kProjectExtension.size()));
    if (!path.endsWith(extension, Qt::CaseInsensitive)) {
        path += extension;
    }
    saveToPath(m_activeProject, path);
}

void Editor::saveToPath(quint64 uid, const QString& path) {
    const OpenProject* project = findProject(uid);
    if (m_fileBusy || !project) {
        return;
    }
    setFileBusy(true);
    const std::shared_ptr<const sharkcut::Document> snapshot = project->history.snapshot();
    const sharkcut::Workspace workspace = workspaceOf(*project);
    QPointer<Editor> self(this);
    QThreadPool::globalInstance()->start([self, uid, snapshot, workspace, path] {
        QString error;
        try {
            sharkcut::saveDocument(*snapshot, workspace,
                                   sharkcut::pathFromUtf8(path.toStdString()));
        } catch (const std::exception& e) {
            error = QString::fromStdString(e.what());
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, uid, snapshot, path, error] {
                if (self) {
                    self->finishSave(uid, snapshot, path, error);
                }
            },
            Qt::QueuedConnection);
    });
}

void Editor::finishSave(quint64 uid, const std::shared_ptr<const sharkcut::Document>& written,
                        const QString& path, const QString& error) {
    setFileBusy(false);
    OpenProject* project = findProject(uid);
    if (!error.isEmpty() || !project) {
        setError(error);
        emit saved(false);
        return;
    }
    project->history.markSaved(written);
    addRecentProject(path);
    if (!project->history.modified()) {
        removeRecovery(project->recoveryKey); // lo guardado ya está en su archivo
        project->recoveryKey.clear();
        project->autosaved.reset();
    }
    if (project->path != path) {
        const bool firstSave = project->path.isEmpty();
        project->path = path;
        if (uid == m_activeProject) {
            updateWaveformDirectory();
        }
        if (firstSave) {
            // Las ondas calculadas mientras no tenía archivo, a su caché.
            QStringList media;
            for (const MediaItem& item : written->media) {
                media << QString::fromStdString(item.path);
            }
            m_waveforms.saveTo(path + QStringLiteral(".cache/ondas"), media);
        }
        emit fileChanged();
    }
    emit modifiedChanged();
    emit projectsChanged();
    emit saved(true);
}

// --- Exportación ----------------------------------------------------------------------

QVariantMap Editor::completeExportSettings(QVariantMap settings) {
    const QVariantMap defaults{
        {QStringLiteral("codec"), QStringLiteral("h264")},
        {QStringLiteral("scale"), 1.0},
        {QStringLiteral("fps"), 0},
        {QStringLiteral("quality"), 1},
        {QStringLiteral("mbps"), 16.0},
        {QStringLiteral("audio"), true},
        {QStringLiteral("audioKbps"), 192},
        {QStringLiteral("folder"), QString()},
    };
    for (auto it = defaults.begin(); it != defaults.end(); ++it) {
        if (!settings.contains(it.key())) {
            settings.insert(it.key(), it.value());
        }
    }
    return settings;
}

void Editor::exportSequence(const QVariantMap& options) {
    if (m_exporter) {
        return;
    }
    const auto id = static_cast<ObjectId>(options.value(QStringLiteral("sequence")).toLongLong());
    const ObjectId sequence = id != 0 ? id : activeSequenceId();
    if (!document().sequence(sequence)) {
        setError(QStringLiteral("No hay ninguna secuencia que exportar"));
        return;
    }
    QVariantMap settings = completeExportSettings(m_exportSettings);
    for (const QString& key :
         {QStringLiteral("codec"), QStringLiteral("scale"), QStringLiteral("fps"),
          QStringLiteral("quality"), QStringLiteral("mbps"), QStringLiteral("audio"),
          QStringLiteral("audioKbps"), QStringLiteral("folder")}) {
        if (options.contains(key)) {
            settings.insert(key, options.value(key));
        }
    }
    const QUrl folderUrl = settings.value(QStringLiteral("folder")).toUrl();
    const QString folder = folderUrl.isLocalFile() ? folderUrl.toLocalFile() : folderUrl.toString();
    const QString name = options.value(QStringLiteral("name")).toString().trimmed();
    if (folder.isEmpty() || name.isEmpty()) {
        return;
    }
    m_exportSettings = settings;
    QSettings().setValue(QLatin1String(kExportSettingsKey), m_exportSettings);
    emit exportSettingsChanged();
    startExport(document().view(sequence), settings, folder, name,
                options.value(QStringLiteral("rangeStart"), -1).toLongLong(),
                options.value(QStringLiteral("rangeEnd"), -1).toLongLong());
}

void Editor::quickExport() {
    if (m_exporter || !hasSequence()) {
        return;
    }
    QVariantMap settings = completeExportSettings(m_exportSettings);
    QUrl folder = settings.value(QStringLiteral("folder")).toUrl();
    if (folder.isEmpty()) {
        folder = defaultExportFolder();
    }
    QString name = sequenceName().trimmed();
    // Caracteres que no valen en un nombre de archivo de Windows.
    static const QRegularExpression invalid(QStringLiteral("[<>:\"/\\\\|?*]"));
    name.replace(invalid, QStringLiteral("_"));
    startExport(project(), settings,
                folder.isLocalFile() ? folder.toLocalFile() : folder.toString(),
                name.isEmpty() ? projectName() : name, -1, -1);
}

void Editor::startExport(const Project& sequence, const QVariantMap& settings,
                         const QString& folder, const QString& name, qint64 rangeStart,
                         qint64 rangeEnd) {
    if (m_exporter || sequence.length() <= 0) {
        setError(QStringLiteral("La secuencia está vacía: no hay nada que exportar"));
        return;
    }
    QString path = QDir(folder).filePath(name);
    if (!path.endsWith(QLatin1String(".mp4"), Qt::CaseInsensitive)) {
        path += QLatin1String(".mp4");
    }
    pause();

    const sharkcut::VideoCodec codec = codecOf(settings.value(QStringLiteral("codec")).toString());
    const int fps = settings.value(QStringLiteral("fps")).toInt();
    const sharkcut::ExportSize size = sharkcut::exportSize(
        sequence.settings.width, sequence.settings.height,
        std::clamp(settings.value(QStringLiteral("scale")).toDouble(), 0.1, 4.0));
    const int quality = settings.value(QStringLiteral("quality")).toInt();

    sharkcut::ExportJob job;
    job.content = sequence;
    job.rate = fps > 0 ? sharkcut::FrameRate{fps, 1} : sequence.settings.frameRate;
    job.canvasSize = QSize(size.width, size.height);
    job.rangeStart = std::max<qint64>(0, rangeStart);
    job.rangeEnd = rangeEnd;
    job.outputPath = path.toStdString();
    job.codec = codec;
    job.videoBitrate =
        quality >= 0 && quality < sharkcut::kExportQualityCount
            ? sharkcut::recommendedBitrate(size.width, size.height, job.rate, quality, codec)
            : static_cast<std::int64_t>(
                  std::clamp(settings.value(QStringLiteral("mbps")).toDouble(), 0.5, 400.0) * 1e6);
    job.audio = settings.value(QStringLiteral("audio")).toBool();
    job.audioBitrate =
        std::clamp(settings.value(QStringLiteral("audioKbps")).toInt(), 64, 320) * 1000;
#ifndef Q_OS_WIN
    // OpenGL necesita una superficie creada en el hilo de la interfaz.
    if (!m_exportSurface) {
        m_exportSurface.reset(QRhiGles2InitParams::newFallbackSurface());
    }
    job.fallbackSurface = m_exportSurface.get();
#endif

    const quint64 session = ++m_exportSession;
    QPointer<Editor> self(this);
    auto post = [self, session](auto fn) {
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, session, fn = std::move(fn)]() {
                if (self && self->m_exportSession == session) {
                    fn(*self);
                }
            },
            Qt::QueuedConnection);
    };

    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    sharkcut::SequenceExporter::Callbacks callbacks;
    callbacks.progress = [post](std::int64_t done, std::int64_t total) {
        post([done, total](Editor& editor) {
            editor.m_exportProgress = static_cast<double>(done) / static_cast<double>(total);
            editor.m_exportMessage =
                QStringLiteral("Exportando… %1 / %2 fotogramas").arg(done).arg(total);
            emit editor.exportChanged();
        });
    };
    callbacks.finished = [post, timer](bool ok, const std::string& message,
                                       const std::string& encoder, const std::string& written) {
        const QString text = QString::fromStdString(message);
        QString codecName = QString::fromStdString(encoder);
        if (sharkcut::isSoftwareEncoder(encoder)) {
            codecName += QStringLiteral(" (por software: la GPU no estaba disponible)");
        }
        const QString file = QString::fromStdString(written);
        const double seconds = static_cast<double>(timer->elapsed()) / 1000.0;
        post([ok, text, codecName, file, seconds](Editor& editor) {
            editor.m_exporter.reset(); // el hilo ya terminó
            editor.m_exportProgress = ok ? 1.0 : 0.0;
            if (ok) {
                editor.m_lastExportPath = file;
            }
            editor.m_exportMessage = ok ? QStringLiteral("Exportado en %1 s con %2: %3")
                                              .arg(seconds, 0, 'f', 1)
                                              .arg(codecName, QDir::toNativeSeparators(file))
                                        : QStringLiteral("No se exportó: %1").arg(text);
            emit editor.exportChanged();
        });
    };

    m_exportProgress = 0.0;
    m_exportMessage = QStringLiteral("Preparando la exportación…");
    m_exporter = std::make_unique<sharkcut::SequenceExporter>(std::move(job), std::move(callbacks));
    emit exportChanged();
}

void Editor::showLastExport() const {
    if (!m_lastExportPath.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_lastExportPath).absolutePath()));
    }
}

QVariantList Editor::exportSizes(qint64 sequenceId) const {
    QVariantList list;
    const sharkcut::Sequence* sequence = document().sequence(static_cast<ObjectId>(sequenceId));
    if (!sequence) {
        return list;
    }
    for (const sharkcut::ExportSize& size :
         sharkcut::exportSizes(sequence->settings.width, sequence->settings.height)) {
        QVariantMap entry;
        entry[QStringLiteral("width")] = size.width;
        entry[QStringLiteral("height")] = size.height;
        entry[QStringLiteral("scale")] = size.scale;
        entry[QStringLiteral("label")] =
            QStringLiteral("%1×%2").arg(size.width).arg(size.height) +
            (size.scale == 1.0 ? QStringLiteral(" (la de la secuencia)")
                               : QStringLiteral(" (%1 %)").arg(std::lround(size.scale * 100)));
        list.append(entry);
    }
    return list;
}

QVariantMap Editor::exportPreset(qint64 sequenceId, const QString& name) const {
    const sharkcut::Sequence* sequence = document().sequence(static_cast<ObjectId>(sequenceId));
    if (!sequence) {
        return {};
    }
    const sharkcut::ProjectSettings& s = sequence->settings;
    const bool tiktok = name == QLatin1String("tiktok");
    const sharkcut::ExportPresetChoice choice = sharkcut::exportPreset(
        tiktok ? sharkcut::ExportPreset::TikTok : sharkcut::ExportPreset::YouTube, s.width,
        s.height, s.frameRate);
    const sharkcut::ExportSize size = sharkcut::exportSize(s.width, s.height, choice.scale);
    const QString fps = choice.fps > 0 ? QString::number(choice.fps) : fpsText(s.frameRate);
    QString note;
    QString warning;
    if (tiktok) {
        note = QStringLiteral("Para TikTok: %1×%2 en H.264, calidad Alta, a %3 fps.")
                   .arg(size.width)
                   .arg(size.height)
                   .arg(fps);
        if (choice.notVertical) {
            warning = QStringLiteral("La secuencia no es vertical (%1×%2): se exporta tal cual, "
                                     "sin recortar. Para TikTok, haz una versión vertical (clic "
                                     "derecho en la secuencia → «Duplicar como vertical "
                                     "(9:16)»).")
                          .arg(s.width)
                          .arg(s.height);
        }
    } else {
        note = std::min(s.width, s.height) >= 1440
                   ? QStringLiteral("Para YouTube: %1×%2, calidad Alta. La secuencia ya es de "
                                    "1440p o más, así que se exporta a su tamaño.")
                         .arg(size.width)
                         .arg(size.height)
                   : QStringLiteral("Para YouTube: %1×%2, calidad Alta. Subir en 1440p aunque el "
                                    "lienzo sea más pequeño (p. ej. 1080p) mejora el resultado: "
                                    "YouTube da más calidad a los videos de 1440p o más al "
                                    "recomprimirlos.")
                         .arg(size.width)
                         .arg(size.height);
    }
    return {{QStringLiteral("scale"), choice.scale},
            {QStringLiteral("codec"), choice.codec == sharkcut::VideoCodec::H265
                                          ? QStringLiteral("h265")
                                          : QStringLiteral("h264")},
            {QStringLiteral("quality"), choice.quality},
            {QStringLiteral("fps"), choice.fps},
            {QStringLiteral("note"), note},
            {QStringLiteral("warning"), warning}};
}

double Editor::recommendedMbps(qint64 sequenceId, double scale, int fps, int quality,
                               const QString& codec) const {
    const sharkcut::Sequence* sequence = document().sequence(static_cast<ObjectId>(sequenceId));
    if (!sequence) {
        return 0.0;
    }
    const sharkcut::ExportSize size =
        sharkcut::exportSize(sequence->settings.width, sequence->settings.height, scale);
    const sharkcut::FrameRate rate =
        fps > 0 ? sharkcut::FrameRate{fps, 1} : sequence->settings.frameRate;
    return static_cast<double>(sharkcut::recommendedBitrate(size.width, size.height, rate, quality,
                                                            codecOf(codec))) /
           1e6;
}

QString Editor::sequenceTimecode(qint64 sequenceId, qint64 frame) const {
    const sharkcut::Sequence* sequence = document().sequence(static_cast<ObjectId>(sequenceId));
    if (!sequence) {
        return {};
    }
    return QString::fromStdString(sharkcut::formatTimecode(frame, sequence->settings.frameRate));
}

qint64 Editor::parseTimecode(qint64 sequenceId, const QString& text) const {
    const sharkcut::Sequence* sequence = document().sequence(static_cast<ObjectId>(sequenceId));
    if (!sequence) {
        return -1;
    }
    const auto frame = sharkcut::parseTimecode(text.toStdString(), sequence->settings.frameRate);
    return frame ? static_cast<qint64>(*frame) : -1;
}

// --- Proyectos recientes ------------------------------------------------------------

void Editor::addRecentProject(const QString& path) {
    const QString absolute = QFileInfo(path).absoluteFilePath();
    QSettings settings;
    QStringList list = settings.value(QLatin1String(kRecentProjectsKey)).toStringList();
    list.removeAll(absolute);
    list.prepend(absolute);
    while (list.size() > kMaxRecentProjects) {
        list.removeLast();
    }
    settings.setValue(QLatin1String(kRecentProjectsKey), list);
    refreshRecentProjects();
}

void Editor::forgetRecentProject(const QString& path) {
    QSettings settings;
    QStringList list = settings.value(QLatin1String(kRecentProjectsKey)).toStringList();
    list.removeAll(path);
    settings.setValue(QLatin1String(kRecentProjectsKey), list);
    refreshRecentProjects();
}

void Editor::refreshRecentProjects() {
    const QStringList paths = QSettings().value(QLatin1String(kRecentProjectsKey)).toStringList();
    // Mirar si existen es acceso a disco (quizá a una unidad de red): fuera de la interfaz.
    QPointer<Editor> self(this);
    QThreadPool::globalInstance()->start([self, paths] {
        QVariantList list;
        for (const QString& path : paths) {
            const QFileInfo info(path);
            QVariantMap entry;
            entry[QStringLiteral("path")] = path;
            entry[QStringLiteral("name")] = info.completeBaseName();
            entry[QStringLiteral("folder")] = QDir::toNativeSeparators(info.absolutePath());
            entry[QStringLiteral("exists")] = info.exists();
            entry[QStringLiteral("date")] =
                info.exists()
                    ? QLocale().toString(info.lastModified(), QStringLiteral("d MMM yyyy, HH:mm"))
                    : QString();
            list.append(entry);
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, list] {
                if (self) {
                    self->m_recentProjects = list;
                    emit self->recentProjectsChanged();
                }
            },
            Qt::QueuedConnection);
    });
}

void Editor::cancelExport() {
    if (m_exporter) {
        m_exporter->cancel(); // el aviso de "terminado" llega después
    }
}

// --- Biblioteca ---------------------------------------------------------------------

void Editor::importFiles(const QList<QUrl>& urls, qint64 placeAt, int track, qint64 folder) {
    QStringList paths;
    for (const QUrl& url : urls) {
        const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
        if (!path.isEmpty()) {
            paths << path;
        }
    }
    importPaths(paths, placeAt, track, folder);
}

void Editor::importPaths(const QStringList& paths, qint64 placeAt, int track, qint64 folder) {
    if (paths.isEmpty()) {
        return;
    }
    ++m_importing;
    emit importingChanged();

    // El análisis abre los archivos: fuera del hilo de la interfaz.
    QPointer<Editor> self(this);
    const quint64 uid = m_activeProject;
    const auto stillUs = static_cast<std::int64_t>(std::llround(stillSeconds() * 1e6));
    QThreadPool::globalInstance()->start([self, uid, paths, placeAt, track, folder, stillUs] {
        auto items = std::make_shared<std::vector<MediaItem>>();
        QStringList errors;
        for (const QString& path : paths) {
            try {
                const sharkcut::MediaInfo info = sharkcut::probeMedia(path.toStdString());
                MediaItem item;
                item.path = path.toStdString();
                item.name = QFileInfo(path).fileName().toStdString();
                item.durationUs = info.durationUs;
                item.hasAudio = !info.audio.empty();
                if (info.video && info.video->still) {
                    // Imagen fija: dura lo elegido para las imágenes (luego se alarga).
                    item.hasVideo = true;
                    item.still = true;
                    item.hasAudio = false;
                    item.width = info.video->width;
                    item.height = info.video->height;
                    item.durationUs = stillUs;
                } else if (info.video) {
                    item.hasVideo = true;
                    item.frameRate = info.video->frameRate;
                    item.width = info.video->width;
                    item.height = info.video->height;
                    if (info.video->frameCount > 0) {
                        item.durationUs = sharkcut::frameToMicroseconds(info.video->frameCount,
                                                                        info.video->frameRate);
                    }
                }
                if (!item.hasVideo && !item.hasAudio) {
                    throw sharkcut::MediaError("no tiene video ni audio");
                }
                items->push_back(std::move(item));
            } catch (const std::exception& e) {
                errors << QStringLiteral("%1 (%2)").arg(QFileInfo(path).fileName(),
                                                        QString::fromStdString(e.what()));
            }
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, uid, items, errors, placeAt, track, folder] {
                if (self) {
                    self->finishImport(uid, std::move(*items), errors, placeAt, track, folder);
                }
            },
            Qt::QueuedConnection);
    });
}

void Editor::finishImport(quint64 uid, std::vector<MediaItem> items, QStringList errors,
                          qint64 placeAt, int track, qint64 folder) {
    --m_importing;
    emit importingChanged();
    if (!errors.isEmpty()) {
        setError(QStringLiteral("No se pudo importar: %1").arg(errors.join(QStringLiteral("; "))));
    }
    // El proyecto donde se pidió; si mientras tanto se cerró, no hay dónde añadirlos.
    OpenProject* project = findProject(uid);
    if (items.empty() || !project) {
        return;
    }
    // Solo se colocan en la línea de tiempo si sigue a la vista la misma secuencia.
    const ObjectId sequence = uid == m_activeProject ? activeSequenceId() : 0;
    const bool place = placeAt >= 0 && sequence != 0;
    std::int64_t end = -1;
    const QString label =
        place ? QStringLiteral("Añadir a la línea de tiempo") : QStringLiteral("Importar");
    const sharkcut::PlaceTarget target = place ? placeTarget(track) : sharkcut::PlaceTarget{};
    const bool changed = project->history.apply(label.toStdString(), [&](sharkcut::Document& d) {
        bool any = false;
        std::vector<ObjectId> ids;
        for (MediaItem item : items) {
            const auto found =
                std::find_if(d.media.begin(), d.media.end(),
                             [&item](const MediaItem& m) { return m.path == item.path; });
            if (found != d.media.end()) {
                ids.push_back(found->id);
                continue;
            }
            item.id = d.nextId++;
            item.folder =
                d.folder(static_cast<ObjectId>(folder)) ? static_cast<ObjectId>(folder) : 0;
            ids.push_back(item.id);
            d.media.push_back(std::move(item));
            any = true;
        }
        if (place) {
            sharkcut::edit(d, sequence, [&](Project& p) {
                std::int64_t at = placeAt;
                for (const ObjectId id : ids) {
                    adoptSettings(p, id);
                    if (const ObjectId clip = sharkcut::placeClip(p, id, at, target)) {
                        at = p.clip(clip)->end();
                        end = at;
                        any = true;
                    }
                }
                return true;
            });
        }
        return any;
    });
    if (!changed) {
        return;
    }
    if (uid == m_activeProject) {
        onProjectChanged();
        updateWaveformDirectory(); // ¿alguno de los nuevos tiene ya su proxy?
    }
    emit projectsChanged();
    if (end >= 0) {
        seek(end);
    }
}

void Editor::removeMedia(qint64 mediaId) {
    editDocument(QStringLiteral("Quitar medio"), [mediaId](sharkcut::Document& d) {
        return sharkcut::removeMediaEverywhere(d, static_cast<ObjectId>(mediaId));
    });
}

QString Editor::mediaPath(qint64 mediaId) const {
    const MediaItem* item = project().findMedia(static_cast<ObjectId>(mediaId));
    return item ? QString::fromStdString(item->path) : QString();
}

// --- Edición ------------------------------------------------------------------------

bool Editor::edit(const QString& label, const std::function<bool(Project&)>& change) {
    const ObjectId sequence = activeSequenceId();
    if (sequence == 0) {
        return false;
    }
    if (!current().history.apply(label.toStdString(), [&](sharkcut::Document& d) {
            const ObjectId firstNew = d.nextId;
            if (!sharkcut::edit(d, sequence, change)) {
                return false;
            }
            fileNewResources(d, firstNew);
            return true;
        })) {
        return false;
    }
    onProjectChanged();
    return true;
}

bool Editor::editDocument(const QString& label,
                          const std::function<bool(sharkcut::Document&)>& change) {
    if (!current().history.apply(label.toStdString(), change)) {
        return false;
    }
    onProjectChanged();
    emit projectsChanged();
    return true;
}

sharkcut::PlaceTarget Editor::placeTarget(int track) const {
    const Project& p = project();
    if (track == kNewTracks) {
        // Números que aún no existen: placeClip crea una pista de cada tipo.
        return {p.trackCount(sharkcut::TrackKind::Video), p.trackCount(sharkcut::TrackKind::Audio)};
    }
    if (track < 0 || static_cast<std::size_t>(track) >= p.tracks.size()) {
        return {};
    }
    // La pista donde se suelta y su pareja del otro tipo (V2 ↔ A2).
    const std::size_t number = p.trackNumber(static_cast<std::size_t>(track));
    return {number, number};
}

// --- Pistas -------------------------------------------------------------------------

void Editor::addTrack(const QString& kind) {
    const bool video = kind == QLatin1String("video");
    edit(video ? QStringLiteral("Añadir pista de video") : QStringLiteral("Añadir pista de audio"),
         [video](Project& p) {
             sharkcut::addTrack(p, video ? sharkcut::TrackKind::Video : sharkcut::TrackKind::Audio);
             return true;
         });
}

void Editor::removeTrack(int track) {
    const QString name =
        QString::fromStdString(project().trackName(static_cast<std::size_t>(track)));
    edit(QStringLiteral("Quitar la pista %1").arg(name), [track](Project& p) {
        return track >= 0 && sharkcut::removeTrack(p, static_cast<std::size_t>(track));
    });
}

void Editor::setTrackHidden(int track, bool hidden) {
    edit(hidden ? QStringLiteral("Ocultar pista") : QStringLiteral("Mostrar pista"),
         [track, hidden](Project& p) {
             return track >= 0 &&
                    sharkcut::setTrackHidden(p, static_cast<std::size_t>(track), hidden);
         });
}

void Editor::setTrackMuted(int track, bool muted) {
    edit(muted ? QStringLiteral("Silenciar pista") : QStringLiteral("Activar sonido de la pista"),
         [track, muted](Project& p) {
             return track >= 0 &&
                    sharkcut::setTrackMuted(p, static_cast<std::size_t>(track), muted);
         });
}

void Editor::setTrackLocked(int track, bool locked) {
    edit(locked ? QStringLiteral("Bloquear pista") : QStringLiteral("Desbloquear pista"),
         [track, locked](Project& p) {
             return track >= 0 &&
                    sharkcut::setTrackLocked(p, static_cast<std::size_t>(track), locked);
         });
}

// --- Clips --------------------------------------------------------------------------

void Editor::addMedia(qint64 mediaId, qint64 frame, int track) {
    std::int64_t end = -1;
    const sharkcut::PlaceTarget target = placeTarget(track);
    const bool placed = edit(QStringLiteral("Añadir clip"), [&](Project& p) {
        const auto id = static_cast<ObjectId>(mediaId);
        adoptSettings(p, id);
        const ObjectId clip = sharkcut::placeClip(p, id, frame < 0 ? p.length() : frame, target);
        if (clip != 0) {
            end = p.clip(clip)->end();
        }
        return clip != 0;
    });
    if (placed) {
        seek(end);
    }
}

void Editor::splitAtPlayhead() {
    const std::int64_t frame = m_currentFrame;
    edit(QStringLiteral("Dividir"), [frame](Project& p) {
        const bool clips = sharkcut::splitAt(p, frame);
        return sharkcut::splitCaptionsAt(p, frame) || clips;
    });
}

void Editor::splitClip(qint64 clipId, qint64 frame) {
    edit(QStringLiteral("Dividir"), [clipId, frame](Project& p) {
        return sharkcut::splitAt(p, frame, {static_cast<ObjectId>(clipId)});
    });
}

void Editor::rippleTrimPrevious() {
    const std::int64_t frame = m_currentFrame;
    const std::int64_t from = sharkcut::previousEdit(project(), frame);
    if (edit(QStringLiteral("Recortar con rizo hasta el cabezal"),
             [frame](Project& p) { return sharkcut::rippleTrimPrevious(p, frame); })) {
        seek(from); // lo que estaba en el cabezal ahora empieza en el borde
    }
}

void Editor::rippleTrimNext() {
    const std::int64_t frame = m_currentFrame;
    if (edit(QStringLiteral("Recortar con rizo desde el cabezal"),
             [frame](Project& p) { return sharkcut::rippleTrimNext(p, frame); })) {
        seek(frame);
    }
}

void Editor::rippleDeleteSelection() {
    if (m_selectedTransition != 0) {
        removeTransition(static_cast<qint64>(m_selectedTransition));
        return;
    }
    const std::vector<ObjectId> selected = m_selection;
    auto result = sharkcut::RippleResult::Nothing;
    edit(QStringLiteral("Eliminar con rizo"), [&](Project& p) {
        result = sharkcut::rippleDelete(p, selected);
        return result != sharkcut::RippleResult::Nothing;
    });
    if (result == sharkcut::RippleResult::Blocked) {
        setError(QStringLiteral("Se quitó, pero el hueco no se cerró: otras pistas tienen clips "
                                "en ese tramo"));
    }
}

bool Editor::hasGapAt(int track, qint64 frame) const {
    const Project& p = project();
    return track >= 0 && static_cast<std::size_t>(track) < p.tracks.size() &&
           !p.tracks[static_cast<std::size_t>(track)].locked &&
           sharkcut::gapAt(p, static_cast<std::size_t>(track), frame).has_value();
}

void Editor::closeGap(int track, qint64 frame) {
    if (track < 0) {
        return;
    }
    auto result = sharkcut::RippleResult::Nothing;
    edit(QStringLiteral("Cerrar hueco"), [&](Project& p) {
        result = sharkcut::closeGap(p, static_cast<std::size_t>(track), frame);
        return result == sharkcut::RippleResult::Closed;
    });
    if (result == sharkcut::RippleResult::Blocked) {
        setError(QStringLiteral("No se puede cerrar el hueco: otras pistas tienen clips en ese "
                                "tramo"));
    }
}

// --- Marcadores ---------------------------------------------------------------------

QVariantList Editor::markers() const {
    QVariantList list;
    for (const sharkcut::Marker& marker : project().markers) {
        QVariantMap entry;
        entry[QStringLiteral("id")] = static_cast<qint64>(marker.id);
        entry[QStringLiteral("frame")] = static_cast<qint64>(marker.frame);
        entry[QStringLiteral("name")] = QString::fromStdString(marker.name);
        list.append(entry);
    }
    return list;
}

void Editor::addMarkerAtPlayhead() {
    const std::int64_t frame = m_currentFrame;
    edit(QStringLiteral("Añadir marcador"),
         [frame](Project& p) { return sharkcut::addMarker(p, frame) != 0; });
}

void Editor::removeMarker(qint64 markerId) {
    edit(QStringLiteral("Quitar marcador"), [markerId](Project& p) {
        return sharkcut::removeMarker(p, static_cast<ObjectId>(markerId));
    });
}

void Editor::renameMarker(qint64 markerId, const QString& name) {
    const std::string text = name.trimmed().toStdString();
    edit(QStringLiteral("Renombrar marcador"), [&](Project& p) {
        return sharkcut::renameMarker(p, static_cast<ObjectId>(markerId), text);
    });
}

void Editor::moveMarker(qint64 markerId, qint64 frame) {
    edit(QStringLiteral("Mover marcador"), [markerId, frame](Project& p) {
        return sharkcut::moveMarker(p, static_cast<ObjectId>(markerId), frame);
    });
}

void Editor::goToMarker(int direction) {
    const std::vector<sharkcut::Marker>& list = project().markers;
    std::optional<std::int64_t> target;
    for (const sharkcut::Marker& marker : list) {
        if (direction > 0 && marker.frame > m_currentFrame) {
            target = marker.frame;
            break;
        }
        if (direction < 0 && marker.frame < m_currentFrame) {
            target = marker.frame; // el último que queda antes
        }
    }
    if (target) {
        pause();
        seek(*target);
    }
}

void Editor::goToEdit(int direction) {
    const Project& p = project();
    std::optional<std::int64_t> target;
    for (const sharkcut::Track& track : p.tracks) {
        for (const Clip& clip : track.clips) {
            for (const std::int64_t edge : {clip.start, clip.end()}) {
                const bool ahead = direction > 0 ? edge > m_currentFrame : edge < m_currentFrame;
                if (ahead && (!target || (direction > 0 ? edge < *target : edge > *target))) {
                    target = edge;
                }
            }
        }
    }
    if (target) {
        pause();
        seek(*target); // el final de la secuencia se muestra en su último fotograma
    }
}

void Editor::openLogFolder() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(logging::folder()));
}

void Editor::copySelection() {
    if (m_selection.empty()) {
        return;
    }
    m_clipboard = sharkcut::copyClips(project(), m_selection);
    emit clipboardChanged();
}

void Editor::cutSelection() {
    if (m_selection.empty()) {
        return;
    }
    copySelection();
    const std::vector<ObjectId> selected = m_selection;
    edit(QStringLiteral("Cortar"),
         [&selected](Project& p) { return sharkcut::removeClips(p, selected); });
}

void Editor::pasteAtPlayhead() {
    if (m_clipboard.empty()) {
        return;
    }
    const std::int64_t at = m_currentFrame;
    std::vector<ObjectId> pasted;
    if (!edit(QStringLiteral("Pegar"), [this, at, &pasted](Project& p) {
            pasted = sharkcut::pasteClips(p, m_clipboard, at);
            return !pasted.empty();
        })) {
        return;
    }
    m_selection = pasted;
    m_selectedCaptions.clear();
    m_selectedTransition = 0;
    emit selectionChanged();
    seek(at + m_clipboard.length);
}

void Editor::removeSelection() {
    if (!m_selectedCaptions.empty()) {
        const std::vector<ObjectId> captions = m_selectedCaptions;
        edit(QStringLiteral("Quitar subtítulos"),
             [&captions](Project& p) { return sharkcut::removeCaptions(p, captions); });
        return;
    }
    if (m_selectedTransition != 0) {
        removeTransition(static_cast<qint64>(m_selectedTransition));
        return;
    }
    const std::vector<ObjectId> selected = m_selection;
    edit(QStringLiteral("Quitar"),
         [&selected](Project& p) { return sharkcut::removeClips(p, selected); });
}

void Editor::trimStart(qint64 clipId, qint64 frame) {
    edit(QStringLiteral("Recortar inicio"), [clipId, frame](Project& p) {
        return sharkcut::trimStart(p, static_cast<ObjectId>(clipId), frame);
    });
}

void Editor::trimEnd(qint64 clipId, qint64 frame) {
    edit(QStringLiteral("Recortar final"), [clipId, frame](Project& p) {
        return sharkcut::trimEnd(p, static_cast<ObjectId>(clipId), frame);
    });
}

qint64 Editor::limitTrimStart(qint64 clipId, qint64 frame) const {
    return sharkcut::limitTrimStart(project(), static_cast<ObjectId>(clipId), frame);
}

qint64 Editor::limitTrimEnd(qint64 clipId, qint64 frame) const {
    return sharkcut::limitTrimEnd(project(), static_cast<ObjectId>(clipId), frame);
}

void Editor::moveClips(qint64 clipId, qint64 delta, int trackShift) {
    const auto id = static_cast<ObjectId>(clipId);
    const auto ref = project().findClip(id);
    if (!ref) {
        return;
    }
    const sharkcut::TrackKind kind = project().tracks[ref->track].kind;
    const std::vector<ObjectId> ids = isSelected(clipId) ? m_selection : std::vector<ObjectId>{id};
    edit(trackShift != 0 ? QStringLiteral("Mover a otra pista") : QStringLiteral("Mover"),
         [&ids, delta, trackShift, kind](Project& p) {
             return sharkcut::moveClips(p, ids, delta, trackShift, kind);
         });
}

qint64 Editor::limitMove(qint64 clipId, qint64 delta) const {
    const auto id = static_cast<ObjectId>(clipId);
    const std::vector<ObjectId> ids = isSelected(clipId) ? m_selection : std::vector<ObjectId>{id};
    return sharkcut::limitMove(project(), ids, delta);
}

int Editor::limitTrackShift(qint64 clipId, int trackShift) const {
    const auto id = static_cast<ObjectId>(clipId);
    const auto ref = project().findClip(id);
    if (!ref) {
        return 0;
    }
    const std::vector<ObjectId> ids = isSelected(clipId) ? m_selection : std::vector<ObjectId>{id};
    return sharkcut::limitTrackShift(project(), ids, project().tracks[ref->track].kind, trackShift);
}

sharkcut::ClipTransform Editor::withValues(sharkcut::ClipTransform t, const QVariantMap& values) {
    auto take = [&values](const char* key, double& member) {
        const auto it = values.find(QLatin1String(key));
        if (it != values.end()) {
            member = it->toDouble();
        }
    };
    take("x", t.x);
    take("y", t.y);
    take("scale", t.scale);
    take("rotation", t.rotation);
    take("opacity", t.opacity);
    take("cropLeft", t.cropLeft);
    take("cropTop", t.cropTop);
    take("cropRight", t.cropRight);
    take("cropBottom", t.cropBottom);
    sharkcut::ChromaKey& key = t.chromaKey;
    if (values.value(QStringLiteral("keyRemove")).toBool()) {
        key = {}; // quitar el efecto del clip
    }
    if (const auto it = values.find(QStringLiteral("keyEnabled")); it != values.end()) {
        key.enabled = it->toBool();
    }
    if (const auto it = values.find(QStringLiteral("keyColor")); it != values.end()) {
        const QColor color(it->toString());
        if (color.isValid()) {
            key.color = static_cast<std::uint32_t>(color.rgb()) & 0xFFFFFFu;
        }
    }
    take("keySimilarity", key.similarity);
    take("keySmoothness", key.smoothness);
    take("keySpill", key.spill);
    // Efectos de imagen
    sharkcut::ClipEffects& fx = t.effects;
    const auto flag = [&values](const char* name, bool& member) {
        const auto it = values.find(QLatin1String(name));
        if (it != values.end()) {
            member = it->toBool();
        }
    };
    flag("grayscaleOn", fx.grayscaleOn);
    take("grayscale", fx.grayscale);
    flag("blurOn", fx.blurOn);
    take("blur", fx.blur);
    flag("cornersOn", fx.cornersOn);
    take("corners", fx.corners);
    flag("shadowOn", fx.shadowOn);
    if (const auto it = values.find(QStringLiteral("shadowColor")); it != values.end()) {
        const QColor color(it->toString());
        if (color.isValid()) {
            fx.shadowColor = static_cast<std::uint32_t>(color.rgb()) & 0xFFFFFFu;
        }
    }
    take("shadowOpacity", fx.shadowOpacity);
    take("shadowSoftness", fx.shadowSoftness);
    take("shadowX", fx.shadowX);
    take("shadowY", fx.shadowY);
    take("shadowSpread", fx.shadowSpread);
    flag("tintOn", fx.tintOn);
    if (const auto it = values.find(QStringLiteral("tintColor")); it != values.end()) {
        const QColor color(it->toString());
        if (color.isValid()) {
            fx.tintColor = static_cast<std::uint32_t>(color.rgb()) & 0xFFFFFFu;
        }
    }
    take("tint", fx.tint);
    return sharkcut::clampTransform(t);
}

void Editor::previewTransform(qint64 clipId, const QVariantMap& values) {
    const auto id = static_cast<ObjectId>(clipId);
    const auto ref = project().findClip(id);
    if (!ref || project().tracks[ref->track].kind != sharkcut::TrackKind::Video ||
        project().tracks[ref->track].locked) {
        return;
    }
    const sharkcut::ClipTransform base = transformOf(id); // la vista previa, si ya la hay
    m_previewClip = id;
    m_previewTransform = withValues(base, values);
    emit selectedClipChanged();
    emit compositionChanged();
}

void Editor::endPreview() {
    if (m_previewClip != 0) {
        m_previewClip = 0;
        emit selectedClipChanged();
        emit compositionChanged();
    }
}

void Editor::setClipTransform(qint64 clipId, const QVariantMap& values, const QString& label) {
    const auto id = static_cast<ObjectId>(clipId);
    const Clip* clip = project().clip(id);
    if (!clip) {
        return;
    }
    // Lo que se previsualizaba es la base: así un arrastre termina donde se soltó.
    const sharkcut::ClipTransform t = withValues(transformOf(id), values);
    m_previewClip = 0;
    const std::int64_t frame = m_currentFrame;
    if (!edit(label.isEmpty() ? QStringLiteral("Transformar") : label, [id, t, frame](Project& p) {
            return sharkcut::setClipTransformAt(p, id, t, frame);
        })) {
        emit selectedClipChanged();
        emit compositionChanged();
    }
}

void Editor::fitClip(qint64 clipId, bool fill) {
    const auto id = static_cast<ObjectId>(clipId);
    const Clip* clip = project().clip(id);
    const MediaItem* item = clip ? project().findMedia(clip->media) : nullptr;
    if (!item || item->width <= 0 || item->height <= 0) {
        return;
    }
    const sharkcut::ClipTransform t =
        sharkcut::fitToCanvas(project().settings, item->width, item->height, transformOf(id), fill);
    m_previewClip = 0;
    const std::int64_t frame = m_currentFrame;
    edit(fill ? QStringLiteral("Rellenar el lienzo") : QStringLiteral("Ajustar al lienzo"),
         [id, t, frame](Project& p) { return sharkcut::setClipTransformAt(p, id, t, frame); });
}

void Editor::rotateClip(qint64 clipId, bool clockwise) {
    const auto id = static_cast<ObjectId>(clipId);
    const Clip* clip = project().clip(id);
    const MediaItem* item = clip ? project().findMedia(clip->media) : nullptr;
    if (clip && clip->isText()) {
        // Un texto solo gira: no hay un video que ajustar al lienzo.
        sharkcut::ClipTransform t = transformOf(id);
        t.rotation += clockwise ? 90.0 : -90.0;
        m_previewClip = 0;
        const std::int64_t frame = m_currentFrame;
        edit(QStringLiteral("Girar 90°"),
             [id, t, frame](Project& p) { return sharkcut::setClipTransformAt(p, id, t, frame); });
        return;
    }
    if (!item || item->width <= 0 || item->height <= 0) {
        return;
    }
    const sharkcut::ProjectSettings& settings = project().settings;
    sharkcut::ClipTransform t = transformOf(id);
    // Si estaba ajustado o rellenando el lienzo, después del giro sigue así; si no (p. ej.
    // una cámara pequeña en una esquina), solo gira.
    const auto same = [](const sharkcut::ClipTransform& a, const sharkcut::ClipTransform& b) {
        return std::abs(a.scale - b.scale) < 1e-3 && std::abs(a.x - b.x) < 0.5 &&
               std::abs(a.y - b.y) < 0.5;
    };
    const bool fitted =
        same(t, sharkcut::fitToCanvas(settings, item->width, item->height, t, false));
    const bool filled =
        same(t, sharkcut::fitToCanvas(settings, item->width, item->height, t, true));
    t.rotation += clockwise ? 90.0 : -90.0;
    if (filled && !fitted) {
        t = sharkcut::fitToCanvas(settings, item->width, item->height, t, true);
    } else if (fitted) {
        t = sharkcut::fitToCanvas(settings, item->width, item->height, t, false);
    }
    m_previewClip = 0;
    const std::int64_t frame = m_currentFrame;
    edit(QStringLiteral("Girar 90°"),
         [id, t, frame](Project& p) { return sharkcut::setClipTransformAt(p, id, t, frame); });
}

void Editor::resetTransform(qint64 clipId) {
    const auto id = static_cast<ObjectId>(clipId);
    const Clip* clip = project().clip(id);
    if (!clip) {
        return;
    }
    // La pantalla verde es un efecto, no parte de la colocación: se conserva.
    sharkcut::ClipTransform t;
    t.chromaKey = clip->transform.chromaKey;
    m_previewClip = 0;
    // También quita los keyframes.
    edit(QStringLiteral("Restablecer transformación"), [id, t](Project& p) {
        const bool animation = sharkcut::setClipAnimation(p, id, {});
        return sharkcut::setClipTransform(p, id, t) || animation;
    });
}

void Editor::applyEffect(qint64 clipId, const QString& effect) {
    const auto id = static_cast<ObjectId>(clipId);
    const auto ref = project().findClip(id);
    if (!ref || project().tracks[ref->track].kind != sharkcut::TrackKind::Video ||
        project().tracks[ref->track].clips[ref->index].isEffect()) {
        return;
    }
    const std::vector<ObjectId> targets = effectTargets(id);
    if (targets.size() <= 1) {
        select(clipId, false);
    }
    if (effect.startsWith(QLatin1String("ajuste:"))) {
        applyClipPreset(clipId, effect.mid(7));
        return;
    }
    if (effect == QLatin1String("croma")) {
        // El color se busca en este clip y va a todos (la misma pantalla verde).
        m_keyTargets = targets.size() > 1 ? targets : std::vector<ObjectId>{};
        detectKeyColor(clipId, true);
        return;
    }
    // Los demás se activan con sus valores (la sombra y el resplandor, con los suyos).
    QVariantMap values;
    QString label;
    if (effect == QLatin1String("byn")) {
        values = {{QStringLiteral("grayscaleOn"), true}, {QStringLiteral("grayscale"), 1.0}};
        label = QStringLiteral("Blanco y negro");
    } else if (effect == QLatin1String("tinte")) {
        values = {{QStringLiteral("tintOn"), true}};
        label = QStringLiteral("Tinte");
    } else if (effect == QLatin1String("desenfoque")) {
        values = {{QStringLiteral("blurOn"), true}};
        label = QStringLiteral("Desenfoque");
    } else if (effect == QLatin1String("esquinas")) {
        values = {{QStringLiteral("cornersOn"), true}};
        label = QStringLiteral("Esquinas redondeadas");
    } else if (effect == QLatin1String("sombra")) {
        values = {
            {QStringLiteral("shadowOn"), true},     {QStringLiteral("shadowColor"), "#000000"},
            {QStringLiteral("shadowOpacity"), 0.6}, {QStringLiteral("shadowSoftness"), 30.0},
            {QStringLiteral("shadowX"), 0.0},       {QStringLiteral("shadowY"), 12.0},
            {QStringLiteral("shadowSpread"), 0.0}};
        label = QStringLiteral("Sombra");
    } else if (effect == QLatin1String("resplandor")) {
        values = {
            {QStringLiteral("shadowOn"), true},     {QStringLiteral("shadowColor"), "#3AA8FF"},
            {QStringLiteral("shadowOpacity"), 0.9}, {QStringLiteral("shadowSoftness"), 40.0},
            {QStringLiteral("shadowX"), 0.0},       {QStringLiteral("shadowY"), 0.0},
            {QStringLiteral("shadowSpread"), 10.0}};
        label = QStringLiteral("Resplandor");
    } else {
        return;
    }
    if (targets.size() > 1) {
        setClipsTransform(targets, values, label);
    } else {
        setClipTransform(clipId, values, label);
    }
    emit effectApplied(effect);
}

std::vector<ObjectId> Editor::effectTargets(ObjectId clip) const {
    const Project& p = project();
    const std::vector<ObjectId> group = sharkcut::withLinked(p, m_selection);
    if (std::find(group.begin(), group.end(), clip) == group.end()) {
        return {clip};
    }
    std::vector<ObjectId> targets;
    for (ObjectId id : group) {
        const auto ref = p.findClip(id);
        if (ref && p.tracks[ref->track].kind == sharkcut::TrackKind::Video &&
            !p.tracks[ref->track].locked && !p.tracks[ref->track].clips[ref->index].isEffect()) {
            targets.push_back(id);
        }
    }
    return targets.empty() ? std::vector<ObjectId>{clip} : targets;
}

void Editor::setClipsTransform(const std::vector<ObjectId>& clips, const QVariantMap& values,
                               const QString& label) {
    m_previewClip = 0;
    const std::int64_t frame = m_currentFrame;
    edit(label, [&clips, &values, frame](Project& p) {
        bool changed = false;
        for (ObjectId id : clips) {
            const Clip* clip = p.clip(id);
            if (!clip) {
                continue;
            }
            // Cada uno con sus valores en el cabezal (o en su primer fotograma).
            const std::int64_t at = std::clamp<std::int64_t>(frame, clip->start, clip->end() - 1);
            const sharkcut::ClipTransform t = withValues(sharkcut::transformAt(*clip, at), values);
            changed = sharkcut::setClipTransformAt(p, id, t, at) || changed;
        }
        return changed;
    });
}

qint64 Editor::videoClipAt(int track, qint64 frame) const {
    const Project& p = project();
    if (track < 0 || static_cast<std::size_t>(track) >= p.tracks.size() ||
        p.tracks[static_cast<std::size_t>(track)].kind != sharkcut::TrackKind::Video) {
        return 0;
    }
    const Clip* clip = p.clipAt(static_cast<std::size_t>(track), frame);
    return clip ? static_cast<qint64>(clip->id) : 0;
}

void Editor::detectKeyColor(qint64 clipId, bool fallbackToGreen) {
    const auto id = static_cast<ObjectId>(clipId);
    const Clip* clip = project().clip(id);
    const MediaItem* item = clip ? project().findMedia(clip->media) : nullptr;
    if (!item || !item->hasVideo || m_detectingKey) {
        return;
    }
    // El fotograma que se ve, o el primero del clip si el cabezal está fuera de él.
    const std::int64_t frame =
        std::clamp<std::int64_t>(m_currentFrame, clip->start, clip->end() - 1);
    const std::int64_t us = sharkcut::frameToMicroseconds(clip->in + (frame - clip->start),
                                                          project().settings.frameRate);
    const std::string path = item->path;
    const quint64 projectUid = m_activeProject;
    m_detectingKey = true;
    emit detectingKeyChanged();

    QPointer<Editor> self(this);
    QThreadPool::globalInstance()->start([self, path, us, id, projectUid, fallbackToGreen] {
        std::optional<std::uint32_t> color;
        try {
            sharkcut::VideoDecoder decoder(path, sharkcut::HwAccel::None);
            decoder.seekToFrame(sharkcut::microsecondsToFrame(us, decoder.info().frameRate));
            if (const auto decoded = decoder.nextFrame()) {
                color = sharkcut::detectKeyColor(*decoded);
            }
        } catch (const std::exception&) {
            // archivo que falta o dañado: no se encuentra color
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, color, id, projectUid, fallbackToGreen] {
                if (!self) {
                    return;
                }
                self->m_detectingKey = false;
                emit self->detectingKeyChanged();
                // Si entretanto se cambió de proyecto, el clip ya no es el mismo.
                const bool same =
                    self->m_activeProject == projectUid && self->project().clip(id) != nullptr;
                const bool found = color && same;
                const std::vector<ObjectId> targets = std::exchange(self->m_keyTargets, {});
                QVariantMap values{{QStringLiteral("keyEnabled"), true}};
                if (found) {
                    values.insert(QStringLiteral("keyColor"),
                                  QStringLiteral("#%1").arg(*color, 6, 16, QLatin1Char('0')));
                }
                if (same && (found || fallbackToGreen)) {
                    if (targets.size() > 1) {
                        self->setClipsTransform(targets, values, QStringLiteral("Pantalla verde"));
                    } else {
                        self->setClipTransform(static_cast<qint64>(id), values,
                                               QStringLiteral("Pantalla verde"));
                    }
                }
                emit self->keyColorDetected(found);
            },
            Qt::QueuedConnection);
    });
}

qint64 Editor::clipAtCanvas(double x, double y) const {
    const auto layers = project().videoLayersAt(m_currentFrame);
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
        if (it->clip->isEffect()) {
            continue; // las transiciones-clip no se seleccionan en el visor
        }
        const sharkcut::ClipTransform t = transformOf(it->clip->id);
        const CanvasBox box = canvasBox(*it->clip, t);
        // El punto, deshaciendo el giro del clip (alrededor del centro del video).
        const double cx = project().settings.width / 2.0 + t.x;
        const double cy = project().settings.height / 2.0 + t.y;
        const double a = -t.rotation * std::numbers::pi / 180.0;
        const double px = cx + (x - cx) * std::cos(a) - (y - cy) * std::sin(a);
        const double py = cy + (x - cx) * std::sin(a) + (y - cy) * std::cos(a);
        if (t.opacity > 0.0 && px >= box.left && px < box.left + box.width && py >= box.top &&
            py < box.top + box.height) {
            return static_cast<qint64>(it->clip->id);
        }
    }
    return 0;
}

void Editor::setClipVolume(qint64 clipId, double volumeDb) {
    const auto id = static_cast<ObjectId>(clipId);
    edit(QStringLiteral("Volumen"),
         [id, volumeDb](Project& p) { return sharkcut::setClipVolume(p, id, volumeDb); });
}

void Editor::unlinkSelection() {
    const std::vector<ObjectId> selected = m_selection;
    edit(QStringLiteral("Desvincular audio y video"),
         [&selected](Project& p) { return sharkcut::unlinkClips(p, selected); });
}

void Editor::undo() {
    if (current().history.undo()) {
        onProjectChanged();
        pruneTabs();
        emit projectsChanged();
    }
}

void Editor::redo() {
    if (current().history.redo()) {
        onProjectChanged();
        pruneTabs();
        emit projectsChanged();
    }
}

void Editor::pruneTabs() {
    for (int i = static_cast<int>(m_tabs.size()) - 1; i >= 0; --i) {
        const Tab& tab = m_tabs[static_cast<std::size_t>(i)];
        const OpenProject* project = findProject(tab.project);
        if (!project || !project->history.state().sequence(tab.sequence)) {
            closeTab(i);
        }
    }
}

qint64 Editor::clipNear(int track, qint64 frame, qint64 maxDistance) const {
    if (track < 0) {
        return 0;
    }
    const Clip* clip = project().nearestClip(static_cast<std::size_t>(track), frame, maxDistance);
    return clip ? static_cast<qint64>(clip->id) : 0;
}

qint64 Editor::captionNear(int track, qint64 frame, qint64 maxDistance) const {
    const Project& p = project();
    if (track < 0 || static_cast<std::size_t>(track) >= p.captionTracks.size()) {
        return 0;
    }
    ObjectId best = 0;
    std::int64_t bestDistance = maxDistance + 1;
    for (const sharkcut::Caption& c : p.captionTracks[static_cast<std::size_t>(track)].captions) {
        const std::int64_t distance = frame < c.start    ? c.start - frame
                                      : frame >= c.end() ? frame - (c.end() - 1)
                                                         : 0;
        if (distance < bestDistance) {
            best = c.id;
            bestDistance = distance;
        }
    }
    return static_cast<qint64>(best);
}

QVariantMap Editor::clipData(qint64 clipId) const {
    const auto ref = project().findClip(static_cast<ObjectId>(clipId));
    return ref ? clipEntry(ref->track, project().tracks[ref->track].clips[ref->index])
               : QVariantMap{};
}

QVariantMap Editor::captionData(qint64 id) const {
    const sharkcut::Caption* c = project().caption(static_cast<ObjectId>(id));
    return c ? QVariantMap{{QStringLiteral("start"), static_cast<qint64>(c->start)},
                           {QStringLiteral("length"), static_cast<qint64>(c->length)}}
             : QVariantMap{};
}

void Editor::select(qint64 clipId, bool add) {
    const auto id = static_cast<ObjectId>(clipId);
    if (!project().clip(id)) {
        return;
    }
    m_selectedTransition = 0;
    m_selectedCaptions.clear();
    if (!add) {
        m_selection = {id};
    } else if (isSelected(clipId)) {
        const std::vector<ObjectId> group = sharkcut::withLinked(project(), {id});
        std::erase_if(m_selection, [&group](ObjectId s) {
            return std::find(group.begin(), group.end(), s) != group.end();
        });
    } else {
        m_selection.push_back(id);
    }
    emit selectionChanged();
}

void Editor::clearSelection() {
    if (!m_selection.empty() || m_selectedTransition != 0 || !m_selectedCaptions.empty()) {
        m_selection.clear();
        m_selectedTransition = 0;
        m_selectedCaptions.clear();
        emit selectionChanged();
    }
}

void Editor::selectArea(const QVariantList& tracks, const QVariantList& captionTracks, qint64 from,
                        qint64 to, bool add) {
    const Project& p = project();
    if (to < from) {
        std::swap(from, to);
    }
    std::vector<ObjectId> clips;
    for (const QVariant& v : tracks) {
        const int t = v.toInt();
        if (t < 0 || static_cast<std::size_t>(t) >= p.tracks.size()) {
            continue;
        }
        for (const Clip& clip : p.tracks[static_cast<std::size_t>(t)].clips) {
            if (clip.start < to && clip.end() > from) {
                clips.push_back(clip.id);
            }
        }
    }
    std::vector<ObjectId> captions;
    if (clips.empty()) {
        for (const QVariant& v : captionTracks) {
            const int t = v.toInt();
            if (t < 0 || static_cast<std::size_t>(t) >= p.captionTracks.size()) {
                continue;
            }
            for (const sharkcut::Caption& c :
                 p.captionTracks[static_cast<std::size_t>(t)].captions) {
                if (c.start < to && c.end() > from) {
                    captions.push_back(c.id);
                }
            }
        }
    }
    m_selectedTransition = 0;
    if (!clips.empty() || !add) {
        m_selectedCaptions.clear();
    }
    if (!captions.empty() || !add) {
        m_selection.clear();
    }
    for (ObjectId id : clips) {
        if (std::find(m_selection.begin(), m_selection.end(), id) == m_selection.end()) {
            m_selection.push_back(id);
        }
    }
    for (ObjectId id : captions) {
        if (std::find(m_selectedCaptions.begin(), m_selectedCaptions.end(), id) ==
            m_selectedCaptions.end()) {
            m_selectedCaptions.push_back(id);
        }
    }
    m_captionPreview.reset();
    emit selectionChanged();
}

// --- Transiciones -------------------------------------------------------------------

QVariantList Editor::transitions() const {
    QVariantList list;
    const Project& p = project();
    for (std::size_t t = 0; t < p.tracks.size(); ++t) {
        for (const sharkcut::Transition& tr : p.tracks[t].transitions) {
            QVariantMap entry;
            entry[QStringLiteral("id")] = static_cast<qint64>(tr.id);
            entry[QStringLiteral("track")] = static_cast<int>(t);
            entry[QStringLiteral("cut")] = static_cast<qint64>(p.cutOf(tr));
            entry[QStringLiteral("before")] = static_cast<qint64>(tr.before);
            entry[QStringLiteral("after")] = static_cast<qint64>(tr.after);
            entry[QStringLiteral("name")] = fromView(sharkcut::transitionInfo(tr.params.type).name);
            entry[QStringLiteral("sound")] = tr.sound != 0;
            entry[QStringLiteral("locked")] = p.tracks[t].locked;
            const sharkcut::TransitionLimits limits = sharkcut::transitionLimits(p, tr.id);
            entry[QStringLiteral("maxBefore")] = static_cast<qint64>(limits.maxBefore);
            entry[QStringLiteral("maxAfter")] = static_cast<qint64>(limits.maxAfter);
            list.append(entry);
        }
    }
    return list;
}

QStringList Editor::transitionNames() const {
    QStringList names;
    for (const auto& info : sharkcut::kTransitions) {
        names << fromView(info.name);
    }
    return names;
}

QStringList Editor::easingNames() const {
    QStringList names;
    for (std::size_t i = 0; i < sharkcut::kTransitionEasings; ++i) {
        names << fromView(sharkcut::kEasings[i].name);
    }
    return names;
}

QStringList Editor::keyframeEasingNames() const {
    QStringList names;
    for (const sharkcut::Easing e : sharkcut::kKeyframeEasings) {
        names << fromView(sharkcut::kEasings[static_cast<std::size_t>(e)].name);
    }
    return names;
}

QStringList Editor::soundNames() const {
    QStringList names{QStringLiteral("Ninguno")};
    for (const auto& effect : sharkcut::kSoundEffects) {
        names << fromView(effect.name);
    }
    return names;
}

QStringList Editor::transitionSounds() const {
    QStringList names;
    for (const auto& info : sharkcut::kTransitions) {
        const sharkcut::SoundEffect* effect =
            sharkcut::soundEffect(sharkcut::defaultSoundFor(info.type));
        names << (effect ? fromView(effect->name) : QString());
    }
    return names;
}

QVariantMap Editor::selectedTransition() const {
    const Project& p = project();
    const sharkcut::Transition* t = p.transition(m_selectedTransition);
    if (!t) {
        return {};
    }
    const auto ref = p.findTransition(t->id);
    QVariantMap result;
    result[QStringLiteral("id")] = static_cast<qint64>(t->id);
    result[QStringLiteral("name")] = fromView(sharkcut::transitionInfo(t->params.type).name);
    result[QStringLiteral("track")] = QString::fromStdString(p.trackName(ref->track));
    result[QStringLiteral("locked")] = p.tracks[ref->track].locked;
    result[QStringLiteral("typeIndex")] = static_cast<int>(t->params.type);
    result[QStringLiteral("before")] = static_cast<qint64>(t->before);
    result[QStringLiteral("after")] = static_cast<qint64>(t->after);
    result[QStringLiteral("length")] = static_cast<qint64>(t->length());
    result[QStringLiteral("intensity")] = t->params.intensity;
    result[QStringLiteral("blur")] = t->params.blur;
    result[QStringLiteral("easingIndex")] = static_cast<int>(t->params.easing);
    const sharkcut::TransitionLimits limits = sharkcut::transitionLimits(p, t->id);
    result[QStringLiteral("maxBefore")] = static_cast<qint64>(limits.maxBefore);
    result[QStringLiteral("maxAfter")] = static_cast<qint64>(limits.maxAfter);
    // Qué sonido lleva: por el archivo de su clip.
    int soundIndex = 0;
    if (const Clip* sound = p.clip(t->sound)) {
        if (const MediaItem* item = p.findMedia(sound->media)) {
            const QString file = QFileInfo(QString::fromStdString(item->path)).fileName();
            for (std::size_t i = 0; i < sharkcut::kSoundEffects.size(); ++i) {
                if (fromView(sharkcut::kSoundEffects[i].file) == file) {
                    soundIndex = static_cast<int>(i) + 1;
                }
            }
        }
        result[QStringLiteral("soundClip")] = static_cast<qint64>(sound->id);
        result[QStringLiteral("soundVolumeDb")] = sound->volumeDb;
    }
    result[QStringLiteral("soundIndex")] = soundIndex;
    result[QStringLiteral("hasSound")] = t->sound != 0;
    return result;
}

bool Editor::placeSound(Project& p, ObjectId transition, std::string_view soundId) {
    const sharkcut::SoundEffect* effect = sharkcut::soundEffect(soundId);
    if (!effect) {
        return false;
    }
    const ObjectId media = soundMedia(p, *effect);
    const std::int64_t hit = sharkcut::rescale(effect->hitUs, sharkcut::kMicroseconds,
                                               p.settings.frameRate.frameDuration());
    return sharkcut::placeTransitionSound(p, transition, media, hit, kSoundVolumeDb) != 0;
}

void Editor::addTransition(int track, qint64 cut, int typeIndex) {
    if (typeIndex < 0 || typeIndex >= static_cast<int>(sharkcut::kTransitions.size()) ||
        track < 0) {
        return;
    }
    const sharkcut::TransitionInfo& info =
        sharkcut::kTransitions[static_cast<std::size_t>(typeIndex)];
    ObjectId created = 0;
    const bool added = edit(QStringLiteral("Añadir transición"), [&](Project& p) {
        sharkcut::TransitionParams params = info.defaults;
        // Las duraciones del catálogo son para 60 fps: el mismo tiempo en estos fps.
        const sharkcut::FrameRate rate = p.settings.frameRate;
        params.durationFrames = std::max<std::int64_t>(
            2, sharkcut::mulDiv(info.defaults.durationFrames, rate.num, rate.den * 60));
        const sharkcut::Transition* before = nullptr;
        for (const sharkcut::Transition& t :
             p.tracks[static_cast<std::size_t>(track)].transitions) {
            if (p.cutOf(t) == cut) {
                before = &t;
            }
        }
        const ObjectId previousSound = before ? before->sound : 0;
        created = sharkcut::addTransition(p, static_cast<std::size_t>(track), cut, params);
        if (created == 0) {
            return false;
        }
        // El sonido del tipo nuevo sustituye al que hubiera.
        const std::string_view sound = sharkcut::defaultSoundFor(info.type);
        if (!sound.empty()) {
            placeSound(p, created, sound);
        } else if (previousSound != 0) {
            sharkcut::removeClips(p, {previousSound});
        }
        return true;
    });
    if (added) {
        m_selection.clear();
        m_selectedTransition = created;
        emit selectionChanged();
    } else if (created == 0) {
        setError(QStringLiteral("Las transiciones van en el corte entre dos clips seguidos de una "
                                "pista de video"));
    }
}

void Editor::addEffectClip(int track, qint64 frame, int typeIndex) {
    if (typeIndex < 0 || typeIndex >= static_cast<int>(sharkcut::kTransitions.size())) {
        return;
    }
    const sharkcut::TransitionInfo& info =
        sharkcut::kTransitions[static_cast<std::size_t>(typeIndex)];
    ObjectId created = 0;
    edit(QStringLiteral("Añadir transición"), [&](Project& p) {
        const sharkcut::TransitionParams params = effectParams(p, info);
        const sharkcut::FrameRate rate = p.settings.frameRate;
        std::size_t target = 0;
        if (track >= 0 && static_cast<std::size_t>(track) < p.tracks.size() &&
            p.tracks[static_cast<std::size_t>(track)].kind == sharkcut::TrackKind::Video) {
            target = static_cast<std::size_t>(track);
        } else {
            target = p.trackIndex(sharkcut::TrackKind::Video, 0).value_or(0);
        }
        // Centrada en el corte (o borde de clip) más cercano de las pistas de video, si
        // está a menos de medio segundo; si no, empieza donde se soltó.
        const std::int64_t reach =
            std::max<std::int64_t>(1, sharkcut::mulDiv(1, rate.num, 2 * rate.den));
        std::int64_t best = -1;
        for (const sharkcut::Track& t : p.tracks) {
            if (t.kind != sharkcut::TrackKind::Video) {
                continue;
            }
            for (const Clip& c : t.clips) {
                if (c.isEffect()) {
                    continue;
                }
                for (const std::int64_t edge : {c.start, c.end()}) {
                    if (std::abs(edge - frame) <= reach &&
                        (best < 0 || std::abs(edge - frame) < std::abs(best - frame))) {
                        best = edge;
                    }
                }
            }
        }
        const std::int64_t start = best >= 0
                                       ? std::max<std::int64_t>(0, best - params.durationFrames / 2)
                                       : std::max<std::int64_t>(0, frame);
        created = placeEffectWithSound(p, target, start, params);
        return created != 0;
    });
    if (created != 0) {
        select(static_cast<qint64>(created), false);
    }
}

void Editor::addEffectClipAtPlayhead(int typeIndex) {
    if (typeIndex < 0 || typeIndex >= static_cast<int>(sharkcut::kTransitions.size())) {
        return;
    }
    const sharkcut::TransitionInfo& info =
        sharkcut::kTransitions[static_cast<std::size_t>(typeIndex)];
    ObjectId created = 0;
    edit(QStringLiteral("Añadir transición"), [&](Project& p) {
        const sharkcut::TransitionParams params = effectParams(p, info);
        // Su punto medio (el del cambio) en el cabezal, haya un corte ahí o no.
        const std::int64_t start =
            sharkcut::effectStartCenteredAt(m_currentFrame, params.durationFrames);
        const std::size_t v1 = p.trackIndex(sharkcut::TrackKind::Video, 0).value_or(0);
        created = placeEffectWithSound(p, v1, start, params);
        return created != 0;
    });
    if (created != 0) {
        select(static_cast<qint64>(created), false);
    }
}

sharkcut::TransitionParams Editor::effectParams(const Project& p,
                                                const sharkcut::TransitionInfo& info) {
    sharkcut::TransitionParams params = info.defaults;
    // Las duraciones del catálogo son para 60 fps: el mismo tiempo en estos fps.
    const sharkcut::FrameRate rate = p.settings.frameRate;
    params.durationFrames = std::max<std::int64_t>(
        2, sharkcut::mulDiv(info.defaults.durationFrames, rate.num, rate.den * 60));
    return params;
}

ObjectId Editor::placeEffectWithSound(Project& p, std::size_t track, std::int64_t start,
                                      const sharkcut::TransitionParams& params) {
    const ObjectId created = sharkcut::placeEffect(p, track, start, params);
    if (created == 0) {
        return 0;
    }
    // Su efecto de sonido, con el golpe en la mitad de la transición y enlazado a ella
    // (se mueven y se quitan juntos).
    if (const sharkcut::SoundEffect* sound =
            sharkcut::soundEffect(sharkcut::defaultSoundFor(params.type))) {
        const std::int64_t hit = sharkcut::rescale(sound->hitUs, sharkcut::kMicroseconds,
                                                   p.settings.frameRate.frameDuration());
        const ObjectId soundClip = sharkcut::placeSoundHit(
            p, soundMedia(p, *sound), start + params.durationFrames / 2, hit, kSoundVolumeDb);
        if (soundClip != 0) {
            sharkcut::linkEffectSound(p, created, soundClip);
        }
    }
    return created;
}

QVariantList Editor::soundLibrary() const {
    // Por grupos, en este orden (los más usados primero).
    static constexpr std::string_view kOrder[] = {"Clic", "Pop", "Swoosh", "Whoosh", "Impactos"};
    QVariantList list;
    for (const std::string_view category : kOrder) {
        for (const sharkcut::SoundEffect& e : sharkcut::kSoundEffects) {
            if (e.category == category) {
                list.append(QVariantMap{
                    {QStringLiteral("id"), fromView(e.id)},
                    {QStringLiteral("name"), fromView(e.name)},
                    {QStringLiteral("category"), fromView(e.category)},
                    {QStringLiteral("seconds"), static_cast<double>(e.durationUs) / 1e6}});
            }
        }
    }
    return list;
}

void Editor::addSound(const QString& id, int track, qint64 frame) {
    const sharkcut::SoundEffect* effect = sharkcut::soundEffect(id.toStdString());
    if (!effect) {
        return;
    }
    ObjectId created = 0;
    edit(QStringLiteral("Añadir sonido"), [&](Project& p) {
        const ObjectId media = soundMedia(p, *effect);
        const std::int64_t at = std::max<qint64>(0, frame);
        if (track >= 0 && static_cast<std::size_t>(track) < p.tracks.size() &&
            p.tracks[static_cast<std::size_t>(track)].kind == sharkcut::TrackKind::Audio) {
            // En esa pista, empezando donde se soltó (sobrescribe, como un medio).
            const std::size_t number = p.trackNumber(static_cast<std::size_t>(track));
            const MediaItem* item = p.findMedia(media);
            created = sharkcut::placeClip(p, media, at, 0, p.mediaFrames(*item),
                                          {.video = 0, .audio = number});
            if (created != 0) {
                sharkcut::setClipVolume(p, created, kSoundVolumeDb);
            }
        } else {
            created = sharkcut::placeSoundHit(p, media, at, 0, kSoundVolumeDb);
        }
        return created != 0;
    });
    if (created != 0) {
        select(static_cast<qint64>(created), false);
    }
}

void Editor::toggleSoundPreview(const QString& id) {
    if (id == m_previewSound) {
        stopSoundPreview();
        return;
    }
    const sharkcut::SoundEffect* effect = sharkcut::soundEffect(id.toStdString());
    if (!effect) {
        return;
    }
    if (!m_soundPreview) {
        try {
            const bool useNull =
                qEnvironmentVariable("SHARKCUT_AUDIO_DEVICE") == QLatin1String("null");
            m_soundPreview = std::make_shared<sharkcut::SoundPreview>(
                useNull ? sharkcut::AudioOutput::Device::Null
                        : sharkcut::AudioOutput::Device::Default);
        } catch (const std::exception& e) {
            setError(QStringLiteral("No se puede escuchar el sonido: %1")
                         .arg(QString::fromUtf8(e.what())));
            return;
        }
    }
    const quint64 request = ++m_previewRequest;
    m_previewStarted = false;
    m_previewSound = id;
    emit previewSoundChanged();
    m_previewTimer.start();
    // Leer y decodificar el archivo: fuera del hilo de la interfaz.
    const std::string path = QDir(soundFolder()).filePath(fromView(effect->file)).toStdString();
    QPointer<Editor> self(this);
    m_previewPool.start([self, preview = m_soundPreview, path, request] {
        bool ok = true;
        try {
            preview->play(path);
        } catch (const std::exception&) {
            ok = false;
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, preview, request, ok] {
                if (!self || self->m_previewRequest != request) {
                    preview->stop(); // se pidió otro (o parar) mientras se cargaba
                    return;
                }
                if (ok) {
                    self->m_previewStarted = true;
                } else {
                    self->stopSoundPreview();
                }
            },
            Qt::QueuedConnection);
    });
}

void Editor::stopSoundPreview() {
    ++m_previewRequest;
    m_previewTimer.stop();
    m_previewStarted = false;
    if (m_soundPreview) {
        m_soundPreview->stop();
    }
    if (!m_previewSound.isEmpty()) {
        m_previewSound.clear();
        emit previewSoundChanged();
    }
}

void Editor::addSoundAtPlayhead(const QString& id) {
    const sharkcut::SoundEffect* effect = sharkcut::soundEffect(id.toStdString());
    if (!effect) {
        return;
    }
    ObjectId created = 0;
    const std::int64_t at = m_currentFrame;
    edit(QStringLiteral("Añadir sonido"), [&](Project& p) {
        const std::int64_t hit = sharkcut::rescale(effect->hitUs, sharkcut::kMicroseconds,
                                                   p.settings.frameRate.frameDuration());
        created = sharkcut::placeSoundHit(p, soundMedia(p, *effect), at, hit, kSoundVolumeDb);
        return created != 0;
    });
    if (created != 0) {
        select(static_cast<qint64>(created), false);
    }
}

void Editor::setEffectClip(qint64 clipId, const QVariantMap& values) {
    const auto id = static_cast<ObjectId>(clipId);
    const Clip* clip = project().clip(id);
    if (!clip || !clip->isEffect()) {
        return;
    }
    sharkcut::TransitionParams params = *clip->effect;
    if (values.contains(QStringLiteral("typeIndex"))) {
        const int type = values.value(QStringLiteral("typeIndex")).toInt();
        if (type >= 0 && type < static_cast<int>(sharkcut::kTransitions.size()) &&
            type != static_cast<int>(params.type)) {
            // Otro tipo: con sus valores, la misma duración.
            const std::int64_t duration = params.durationFrames;
            params = sharkcut::kTransitions[static_cast<std::size_t>(type)].defaults;
            params.durationFrames = duration;
        }
    }
    if (values.contains(QStringLiteral("intensity"))) {
        params.intensity = values.value(QStringLiteral("intensity")).toDouble();
    }
    if (values.contains(QStringLiteral("blur"))) {
        params.blur = values.value(QStringLiteral("blur")).toDouble();
    }
    if (values.contains(QStringLiteral("easingIndex"))) {
        const int easing = values.value(QStringLiteral("easingIndex")).toInt();
        if (easing >= 0 && easing < static_cast<int>(sharkcut::kTransitionEasings)) {
            params.easing = sharkcut::kEasings[static_cast<std::size_t>(easing)].easing;
        }
    }
    if (values.contains(QStringLiteral("length"))) {
        params.durationFrames = values.value(QStringLiteral("length")).toLongLong();
    }
    edit(QStringLiteral("Cambiar la transición"),
         [id, params](Project& p) { return sharkcut::setClipEffect(p, id, params); });
}

void Editor::removeTransition(qint64 id) {
    const auto transition = static_cast<ObjectId>(id);
    if (edit(QStringLiteral("Quitar transición"),
             [transition](Project& p) { return sharkcut::removeTransition(p, transition); })) {
        if (m_selectedTransition == transition) {
            m_selectedTransition = 0;
            emit selectionChanged();
        }
    }
}

void Editor::selectTransition(qint64 id) {
    const auto transition = static_cast<ObjectId>(id);
    if (!project().transition(transition)) {
        return;
    }
    m_selection.clear();
    m_selectedCaptions.clear();
    m_selectedTransition = transition;
    emit selectionChanged();
}

void Editor::setTransitionType(qint64 id, int typeIndex) {
    if (typeIndex < 0 || typeIndex >= static_cast<int>(sharkcut::kTransitions.size())) {
        return;
    }
    const auto transition = static_cast<ObjectId>(id);
    const sharkcut::TransitionInfo& info =
        sharkcut::kTransitions[static_cast<std::size_t>(typeIndex)];
    edit(QStringLiteral("Tipo de transición"), [&](Project& p) {
        const sharkcut::Transition* t = p.transition(transition);
        if (!t || t->params.type == info.type) {
            return false;
        }
        const ObjectId oldSound = t->sound;
        if (!sharkcut::setTransitionParams(p, transition, info.defaults)) {
            return false;
        }
        const std::string_view sound = sharkcut::defaultSoundFor(info.type);
        if (!sound.empty()) {
            placeSound(p, transition, sound);
        } else if (oldSound != 0) {
            sharkcut::removeClips(p, {oldSound});
        }
        return true;
    });
}

void Editor::setTransitionValue(qint64 id, const QString& key, double value) {
    const auto transition = static_cast<ObjectId>(id);
    const sharkcut::Transition* t = project().transition(transition);
    if (!t) {
        return;
    }
    sharkcut::TransitionParams params = t->params;
    QString label;
    if (key == QLatin1String("intensity")) {
        params.intensity = value;
        label = QStringLiteral("Intensidad de la transición");
    } else if (key == QLatin1String("blur")) {
        params.blur = value;
        label = QStringLiteral("Desenfoque de la transición");
    } else if (key == QLatin1String("easing")) {
        const auto index = static_cast<std::size_t>(std::max(0.0, value));
        if (index >= sharkcut::kTransitionEasings) {
            return;
        }
        params.easing = sharkcut::kEasings[index].easing;
        label = QStringLiteral("Curva de la transición");
    } else {
        return;
    }
    edit(label, [transition, params](Project& p) {
        return sharkcut::setTransitionParams(p, transition, params);
    });
}

void Editor::setTransitionLength(qint64 id, qint64 before, qint64 after) {
    const auto transition = static_cast<ObjectId>(id);
    edit(QStringLiteral("Duración de la transición"), [transition, before, after](Project& p) {
        return sharkcut::setTransitionLength(p, transition, before, after);
    });
}

void Editor::setTransitionSound(qint64 id, int soundIndex) {
    const auto transition = static_cast<ObjectId>(id);
    edit(QStringLiteral("Sonido de la transición"), [&](Project& p) {
        const sharkcut::Transition* t = p.transition(transition);
        if (!t) {
            return false;
        }
        if (soundIndex <= 0) {
            return t->sound != 0 && sharkcut::removeClips(p, {t->sound});
        }
        if (static_cast<std::size_t>(soundIndex) > sharkcut::kSoundEffects.size()) {
            return false;
        }
        return placeSound(p, transition,
                          sharkcut::kSoundEffects[static_cast<std::size_t>(soundIndex) - 1].id);
    });
}

bool Editor::isSelected(qint64 clipId) const {
    const auto id = static_cast<ObjectId>(clipId);
    const std::vector<ObjectId> group = sharkcut::withLinked(project(), m_selection);
    return std::find(group.begin(), group.end(), id) != group.end();
}

void Editor::onProjectChanged() {
    refreshView(); // lo de abajo mira ya la secuencia editada
    // La selección solo conserva los clips que siguen existiendo; su descripción
    // puede cambiar aunque sigan (recortes, movimientos).
    std::erase_if(m_selection, [this](ObjectId id) { return project().clip(id) == nullptr; });
    if (m_previewClip != 0 && !project().clip(m_previewClip)) {
        m_previewClip = 0;
    }
    m_textPreview.reset();
    if (m_selectedTransition != 0 && !project().transition(m_selectedTransition)) {
        m_selectedTransition = 0;
    }
    pruneCaptionSelection();
    emit selectionChanged();

    // Solo se recolocan los hilos de video o se reinicia el audio si cambia lo que
    // tienen que leer: mover la facecam o cambiar un volumen no corta la reproducción.
    const auto now = std::make_shared<const Project>(m_view);
    const bool videoChanged = !m_prepared || !sameVideo(*m_prepared, *now);
    const bool audioChanged = !m_prepared || !sameAudio(*m_prepared, *now);
    m_prepared = now;
    if (!videoChanged && !audioChanged) {
        emit projectChanged();
        emit modifiedChanged();
        emit compositionChanged();
        return;
    }
    const bool wasPlaying = m_playing;
    pause();
    if (videoChanged) {
        resetVideoSlots();
    }
    if (audioChanged) {
        restartAudio();
    }
    emit projectChanged();
    emit modifiedChanged();
    seek(m_currentFrame);
    if (wasPlaying) {
        play();
    }
}

// --- Reproducción --------------------------------------------------------------------

void Editor::restartAudio() {
    m_clock.setAudio(nullptr, nullptr);
    m_audioWorker.reset();
    if (!m_audioOutput || !project().hasAudibleAudio()) {
        return; // sin audio: el reloj usa un temporizador
    }
    const Project copy = project();
    sharkcut::AudioWorker::Callbacks callbacks;
    callbacks.opened = [this](const sharkcut::AudioStreamInfo&) {
        QMetaObject::invokeMethod(
            this, [this] { m_clock.setAudio(m_audioOutput.get(), m_audioWorker.get()); },
            Qt::QueuedConnection);
    };
    callbacks.failed = [this](const std::string& message) {
        const QString text = QString::fromStdString(message);
        QMetaObject::invokeMethod(this, [this, text] { setError(text); }, Qt::QueuedConnection);
    };
    m_audioWorker = std::make_unique<sharkcut::AudioWorker>(
        [copy] { return std::make_unique<sharkcut::TimelineAudioSource>(copy); }, *m_audioOutput,
        std::move(callbacks));
}

void Editor::resetVideoSlots() {
    // Los hilos se conservan (se reutilizan si el medio sigue en uso), pero se recolocan.
    const Project& p = project();
    m_video.resize(p.tracks.size());
    for (std::size_t t = 0; t < m_video.size(); ++t) {
        TrackVideo& track = m_video[t];
        const bool video = p.tracks[t].kind == sharkcut::TrackKind::Video && !p.tracks[t].hidden;
        for (VideoSlot& slot : track.decoders) {
            if (slot.worker && (!video || !p.findMedia(slot.media))) {
                slot.worker.reset();
                slot.media = 0;
            }
            slot.clip = 0;
            slot.seekIndex = -1;
        }
        // Lo último que se veía se conserva mientras llega lo nuevo, si sigue en la pista y
        // leyendo lo mismo (un color sólido que cambia de color, otro proxy… ya no vale: se
        // daría por bueno sin esperar al nuevo).
        for (TrackVideo::Shown& shown : track.shown) {
            const auto ref = p.findClip(shown.clip);
            const sharkcut::Clip* clip = ref ? p.clip(shown.clip) : nullptr;
            const MediaItem* item = clip ? p.findMedia(clip->media) : nullptr;
            if (!video || !ref || ref->track != t || !item || decodePath(*item) != shown.path) {
                if (track.last == shown.frame) {
                    track.last.reset();
                }
                shown = {};
            }
        }
        if (!video) {
            track.last.reset();
        }
    }
}

std::vector<std::size_t> Editor::playedVideoTracks() const {
    std::vector<std::size_t> result;
    const Project& p = project();
    for (std::size_t t = 0; t < p.tracks.size() && t < m_video.size(); ++t) {
        if (p.tracks[t].kind == sharkcut::TrackKind::Video && !p.tracks[t].hidden) {
            result.push_back(t);
        }
    }
    return result;
}

std::vector<Editor::Need> Editor::needsAt(std::size_t track, std::int64_t frame) const {
    const Project& p = project();
    if (const sharkcut::Transition* t = p.transitionAt(track, frame)) {
        const Clip* a = p.clip(t->from);
        const Clip* b = p.clip(t->to);
        if (a && b) {
            return {{a, p.mediaFrameIndex(*a, frame)}, {b, p.mediaFrameIndex(*b, frame)}};
        }
    }
    if (const Clip* clip = p.clipAt(track, frame); clip && clip->hasMedia()) {
        return {{clip, p.mediaFrameIndex(*clip, frame)}};
    }
    return {};
}

std::optional<Editor::Need> Editor::upcoming(std::size_t track, std::int64_t frame) const {
    const Project& p = project();
    const std::vector<Need> now = needsAt(track, frame);
    for (const Clip& clip : p.tracks[track].clips) {
        if (!clip.hasMedia()) {
            continue; // sin video que decodificar
        }
        if (clip.start <= frame || std::any_of(now.begin(), now.end(), [&clip](const Need& n) {
                return n.clip->id == clip.id;
            })) {
            continue;
        }
        // Si entra con una transición, se ve desde antes de su inicio.
        const sharkcut::Transition* into = p.transitionInto(clip.id);
        const std::int64_t first = clip.start - (into ? into->before : 0);
        return Need{&clip, p.mediaFrameIndex(clip, std::max(first, frame + 1))};
    }
    return std::nullopt;
}

int Editor::slotWith(std::size_t track, ObjectId clip) const {
    const auto& decoders = m_video[track].decoders;
    for (int i = 0; i < static_cast<int>(decoders.size()); ++i) {
        if (decoders[static_cast<std::size_t>(i)].worker &&
            decoders[static_cast<std::size_t>(i)].clip == clip) {
            return i;
        }
    }
    return -1;
}

int Editor::slotFor(std::size_t track, const Clip& clip, int avoid) {
    auto& decoders = m_video[track].decoders;
    for (int i = 0; i < static_cast<int>(decoders.size()); ++i) {
        if (i != avoid && decoders[static_cast<std::size_t>(i)].worker &&
            decoders[static_cast<std::size_t>(i)].clip == clip.id) {
            return i;
        }
    }
    // Uno libre, mejor si ya tiene abierto el mismo medio.
    int choice = -1;
    for (int i = 0; i < static_cast<int>(decoders.size()); ++i) {
        if (i != avoid && decoders[static_cast<std::size_t>(i)].worker &&
            decoders[static_cast<std::size_t>(i)].media == clip.media) {
            choice = i;
            break;
        }
    }
    if (choice < 0) {
        choice = avoid == 0 ? 1 : 0;
    }
    VideoSlot& slot = decoders[static_cast<std::size_t>(choice)];
    const MediaItem* item = project().findMedia(clip.media);
    const std::string path = item ? decodePath(*item) : std::string();
    if (!slot.worker || slot.media != clip.media || slot.path != path) {
        slot.worker.reset();
        DecodeWorker::Callbacks callbacks;
        callbacks.failed = [this](const std::string& message) {
            const QString text = QString::fromStdString(message);
            QMetaObject::invokeMethod(this, [this, text] { setError(text); }, Qt::QueuedConnection);
        };
        callbacks.framesAvailable = [this] {
            QMetaObject::invokeMethod(this, [this] { onFramesAvailable(); }, Qt::QueuedConnection);
        };
        // Cuánto tarda en abrirse (en su hilo), para el registro de la sesión.
        callbacks.opened =
            [name = QFileInfo(QString::fromStdString(path)).fileName(), track,
             opening = std::chrono::steady_clock::now()](const sharkcut::VideoStreamInfo&) {
                const double ms = std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - opening)
                                      .count();
                qInfo("Visor: pista %zu abre «%s» en %.1f ms", track + 1, qUtf8Printable(name), ms);
            };
        slot.worker =
            std::make_unique<DecodeWorker>(path, sharkcut::HwAccel::Auto, std::move(callbacks));
        slot.media = clip.media;
        slot.path = path;
    }
    slot.clip = clip.id;
    slot.seekIndex = -1;
    return choice;
}

void Editor::placeSlot(std::size_t track, int slot, std::int64_t index, bool force) {
    VideoSlot& s = m_video[track].decoders[static_cast<std::size_t>(slot)];
    if (force || !s.worker->hasFrame(index)) {
        s.worker->seek(index);
    }
    s.seekIndex = index;
}

bool Editor::continueSlot(std::size_t t, int slot, const Need& need) {
    // La mitad nueva de un corte (dividir durante la reproducción): el hilo que se le da
    // es el del mismo medio, que ya entregó el fotograma que se ve y sigue en orden con
    // los de después. Se le pasa ese fotograma y no se busca nada (sin tirón).
    TrackVideo& track = m_video[t];
    VideoSlot& s = track.decoders[static_cast<std::size_t>(slot)];
    const std::shared_ptr<const sharkcut::VideoFrame> current = track.last;
    // (Se pide en el fotograma del corte o en el siguiente: el de la cola llega ya.)
    if (!current || s.media != need.clip->media || current->index > need.index ||
        need.index - current->index > 1 || !s.worker->continuesAfter(current->index)) {
        return false;
    }
    for (TrackVideo::Shown& shown : track.shown) {
        if (shown.frame == current) {
            shown.clip = need.clip->id;
        }
    }
    s.seekIndex = need.index;
    return true;
}

void Editor::assignTrack(std::size_t t, std::int64_t frame, bool seeking) {
    const std::vector<Need> needs = needsAt(t, frame);
    int used = -1; // hilo ya ocupado por otro clip que se ve
    for (const Need& need : needs) {
        int slot = slotWith(t, need.clip->id);
        if (slot >= 0 && slot != used) {
            // Ya estaba preparado (o reproduciéndose): tras un salto, se recoloca.
            if (seeking) {
                placeSlot(t, slot, need.index, m_playing);
            }
        } else {
            slot = slotFor(t, *need.clip, used);
            if (!continueSlot(t, slot, need)) {
                placeSlot(t, slot, need.index, false);
            }
        }
        used = slot;
    }
    // Con un hilo libre, se prepara lo siguiente en su primer fotograma.
    if (needs.size() < 2) {
        if (const std::optional<Need> next = upcoming(t, frame)) {
            int slot = slotWith(t, next->clip->id);
            if (slot < 0 || slot == used) {
                slot = slotFor(t, *next->clip, used);
            }
            VideoSlot& s = m_video[t].decoders[static_cast<std::size_t>(slot)];
            if (s.seekIndex != next->index) {
                s.worker->seek(next->index);
                s.seekIndex = next->index;
            }
        }
    }
}

void Editor::requestFrames(qint64 frame) {
    for (std::size_t t : playedVideoTracks()) {
        assignTrack(t, frame, true);
    }
}

bool Editor::takeTrackFrame(std::size_t t, qint64 frame) {
    TrackVideo& track = m_video[t];
    const std::vector<Need> needs = needsAt(t, frame);
    if (needs.empty()) {
        track.shown = {}; // hueco: transparente
        track.last.reset();
        return true;
    }
    bool all = true;
    std::array<TrackVideo::Shown, 2> shown;
    for (std::size_t k = 0; k < needs.size() && k < shown.size(); ++k) {
        const Need& need = needs[k];
        shown[k].clip = need.clip->id;
        shown[k].frame = track.frameOf(need.clip->id);
        const int slot = slotWith(t, need.clip->id);
        if (slot < 0) {
            all = false;
            continue;
        }
        VideoSlot& source = track.decoders[static_cast<std::size_t>(slot)];
        DecodeWorker& worker = *source.worker;
        if (auto f = worker.takeUpTo(need.index)) {
            shown[k].frame = std::move(f);
            shown[k].path = source.path;
            // Ya ha dado fotogramas: no sigue en el punto donde se colocó. Si este clip
            // vuelve a ser «el siguiente» (al reproducir otra vez desde antes de él), hay
            // que colocarlo de nuevo; si no, su cola tendría fotogramas de más adelante y
            // al entrar se vería el clip anterior hasta que llegara el bueno.
            source.seekIndex = -1;
        } else {
            for (const TrackVideo::Shown& before : track.shown) {
                if (before.clip == need.clip->id && before.frame == shown[k].frame) {
                    shown[k].path = before.path;
                }
            }
        }
        if (shown[k].frame) {
            track.last = shown[k].frame;
        }
        const bool exact = shown[k].frame && shown[k].frame->index == need.index;
        all = all && (exact || worker.finished());
    }
    track.shown = shown;
    return all;
}

bool Editor::takeFrames(qint64 frame) {
    bool all = true;
    for (std::size_t t : playedVideoTracks()) {
        all = takeTrackFrame(t, frame) && all;
    }
    return all;
}

void Editor::onFramesAvailable() {
    if ((m_playing && m_speed > 0) || m_pendingFrame < 0) {
        return;
    }
    if (takeFrames(m_pendingFrame)) {
        m_pendingFrame = -1;
        setBusy(false);
    }
    emit compositionChanged();
}

void Editor::play() {
    if (frameCount() <= 0 || m_playing) {
        return;
    }
    if (m_currentFrame >= frameCount()) {
        seek(0); // desde el final: vuelve a empezar
    }
    m_speed = 1;
    m_pendingFrame = -1;
    setBusy(false);
    setPlaying(true);
    m_clock.start(sharkcut::frameToMicroseconds(m_currentFrame, project().settings.frameRate));
    if (m_audioOutput) {
        m_audioOutput->takeMissingFrames(); // lo de antes no cuenta
    }
    m_stalls.clear();
    m_stallClock.start();
    const sharkcut::FrameRate rate = project().settings.frameRate;
    m_trace.begin(m_currentFrame, [rate](qint64 f) {
        return QString::fromStdString(sharkcut::formatTimecode(f, rate));
    });
}

void Editor::pause() {
    const bool shuttling = m_playing && m_speed != 1;
    m_trace.end();
    m_clock.stop();
    m_speed = 1;
    setPlaying(false);
    if (shuttling) {
        seek(m_currentFrame); // a otra velocidad quizá se veía un fotograma aproximado
    }
}

void Editor::shuttle(int direction) {
    const qint64 last = frameCount(); // el cabezal llega hasta el final exacto
    if (last <= 0 || direction == 0) {
        return;
    }
    int speed = direction > 0 ? 1 : -1;
    if (m_playing && (m_speed > 0) == (direction > 0)) {
        speed = std::clamp(m_speed * 2, -8, 8); // otra pulsación en el mismo sentido
    }
    if (speed == 1) {
        if (!(m_playing && m_speed == 1)) {
            pause();
            play();
        }
        return;
    }
    if ((speed < 0 && m_currentFrame <= 0) || (speed > 0 && m_currentFrame >= last)) {
        return;
    }
    m_trace.end();
    m_clock.stop(); // a otra velocidad no suena
    m_speed = speed;
    m_shuttleFrom = m_currentFrame;
    m_shuttleTimer.start();
    m_pendingFrame = -1;
    setBusy(false);
    if (m_playing) {
        emit playingChanged(); // cambió la velocidad
    }
    setPlaying(true);
}

void Editor::togglePlay() {
    m_playing ? pause() : play();
}

void Editor::seek(qint64 frame) {
    const qint64 total = frameCount();
    if (total <= 0) {
        setCurrentFrame(0);
        for (TrackVideo& track : m_video) {
            track.shown = {};
            track.last.reset();
        }
        m_pendingFrame = -1;
        setBusy(false);
        emit compositionChanged();
        return;
    }
    // Hasta el final exacto (`total`, justo después del último fotograma): ahí no hay nada
    // que ver y se pega o se añade a continuación, sin pisar el último.
    frame = std::clamp<qint64>(frame, 0, total);
    setCurrentFrame(frame);
    requestFrames(frame);
    if (m_playing && m_speed == 1) {
        m_clock.start(sharkcut::frameToMicroseconds(frame, project().settings.frameRate));
        m_trace.restart(frame);
        m_stalls.clear();
        m_stallClock.restart();
        return;
    }
    if (m_playing) { // J/L: se sigue desde aquí
        m_shuttleFrom = frame;
        m_shuttleTimer.restart();
    }
    // En pausa: se muestra en cuanto lleguen los fotogramas de ese instante.
    if (takeFrames(frame)) {
        m_pendingFrame = -1;
        setBusy(false);
    } else {
        m_pendingFrame = frame;
        setBusy(true);
    }
    emit compositionChanged();
}

void Editor::step(int frames) {
    pause();
    seek(m_currentFrame + frames);
}

void Editor::updateMeters() {
    // Caída suave como en un medidor de verdad: sube al instante, baja 24 dB por segundo.
    const double elapsed = m_meterTimer.isValid() ? m_meterTimer.restart() / 1000.0 : 0.0;
    if (!m_meterTimer.isValid()) {
        m_meterTimer.start();
    }
    sharkcut::AudioOutput::Levels peaks;
    if (m_audioOutput) {
        peaks = m_audioOutput->takePeakLevels();
    }
    const std::array<float, 2> values{peaks.left, peaks.right};
    bool changed = false;
    for (std::size_t c = 0; c < 2; ++c) {
        const double db = values[c] > 0.0f ? 20.0 * std::log10(values[c]) : -60.0;
        const double fallen = m_meter[c] - 24.0 * elapsed;
        const double next = std::clamp(std::max(db, fallen), -60.0, 6.0);
        if (std::abs(next - m_meter[c]) > 0.05) {
            m_meter[c] = next;
            changed = true;
        }
    }
    if (changed) {
        emit metersChanged();
    }
}

void Editor::advance() {
    updateMeters();
    if (!m_playing) {
        return;
    }
    const sharkcut::FrameRate rate = project().settings.frameRate;
    if (m_speed != 1) {
        advanceShuttle(rate);
        return;
    }
    qint64 target = sharkcut::microsecondsToFrame(m_clock.nowUs(), rate);
    const qint64 last = frameCount(); // se para en el final exacto
    const bool reachedEnd = target >= last;
    target = std::min(target, last);

    // Al entrar en otro clip (o en una transición) se usa el hilo que ya estaba preparado.
    for (std::size_t t : playedVideoTracks()) {
        assignTrack(t, target, false);
        takeTrackFrame(t, target);
    }
    rescueStalledSlots(target);
    traceStep(target);
    setCurrentFrame(target);
    emit compositionChanged();
    if (reachedEnd) {
        pause();
    }
}

void Editor::rescueStalledSlots(qint64 frame) {
    // Tras empezar o saltar, el primer fotograma puede tardar: no cuenta.
    constexpr qint64 kGraceMs = 300;
    // Sin fotograma y con el hilo quieto durante este tiempo: se da por atascado.
    constexpr qint64 kStalledMs = 250;
    // Como mucho una recolocación por clip en este tiempo (si la causa es otra, no se
    // repite sin parar).
    constexpr qint64 kRetryMs = 1500;
    const qint64 now = m_stallClock.elapsed();
    std::map<std::pair<std::size_t, ObjectId>, Stall> next;
    for (std::size_t t : playedVideoTracks()) {
        TrackVideo& track = m_video[t];
        for (const Need& need : needsAt(t, frame)) {
            const std::pair key{t, need.clip->id};
            Stall stall;
            if (const auto it = m_stalls.find(key); it != m_stalls.end()) {
                stall = it->second;
            } else {
                stall.sinceMs = now;
            }
            const int slot = slotWith(t, need.clip->id);
            if (slot < 0) {
                next.emplace(key, stall);
                continue;
            }
            const DecodeWorker& worker = *track.decoders[static_cast<std::size_t>(slot)].worker;
            const DecodeWorker::Status status = worker.status();
            const auto shown = track.frameOf(need.clip->id);
            const qint64 shownIndex = shown ? shown->index : -1;
            if (shownIndex != stall.shown || status.nextIndex != stall.next) {
                stall.shown = shownIndex;
                stall.next = status.nextIndex;
                stall.sinceMs = now;
            }
            // Le falta su fotograma y el hilo está parado esperando (cola llena de otros
            // fotogramas o tras servir la caché). Si está abriendo el archivo, buscando o
            // decodificando, va lento pero avanza: recolocarlo lo empeoraría.
            const bool late = shownIndex < need.index && !worker.finished();
            const bool idle = status.opened && status.seekPending < 0 && status.waiting;
            if (late && idle && now >= kGraceMs && now - stall.sinceMs >= kStalledMs &&
                (stall.rescuedMs < 0 || now - stall.rescuedMs >= kRetryMs)) {
                qInfo("Visor: pista %zu, clip %llu sin avanzar %lld ms (pedido %lld, último %lld, "
                      "cola %lld–%lld, siguiente %lld): se vuelve a colocar su hilo",
                      t + 1, static_cast<unsigned long long>(need.clip->id),
                      static_cast<long long>(now - stall.sinceMs),
                      static_cast<long long>(need.index), static_cast<long long>(shownIndex),
                      static_cast<long long>(status.firstQueued),
                      static_cast<long long>(status.lastQueued),
                      static_cast<long long>(status.nextIndex));
                placeSlot(t, slot, need.index, true);
                stall.rescuedMs = now;
                stall.sinceMs = now;
            }
            next.emplace(key, stall);
        }
    }
    m_stalls = std::move(next);
}

void Editor::traceStep(qint64 frame) {
    if (!m_trace.active()) {
        return;
    }
    std::int64_t missing = m_audioOutput ? m_audioOutput->takeMissingFrames() : 0;
    // En los últimos 100 ms de la secuencia el audio se acaba (su último bloque sale
    // incompleto): no es que falten muestras.
    const sharkcut::FrameRate rate = project().settings.frameRate;
    if (frame >= frameCount() - std::max<qint64>(1, sharkcut::mulDiv(rate.num, 1, 10 * rate.den))) {
        missing = 0;
    }
    m_trace.audioMissing(missing);
    std::vector<PlaybackTrace::Visible> visible;
    for (std::size_t t : playedVideoTracks()) {
        const TrackVideo& track = m_video[t];
        for (const Need& need : needsAt(t, frame)) {
            PlaybackTrace::Visible v;
            v.track = t;
            v.clip = need.clip->id;
            v.wanted = need.index;
            if (const auto shown = track.frameOf(need.clip->id)) {
                v.shown = shown->index;
            }
            const int slot = slotWith(t, need.clip->id);
            v.finished =
                slot >= 0 && track.decoders[static_cast<std::size_t>(slot)].worker->finished();
            visible.push_back(v);
        }
    }
    m_trace.step(frame, visible,
                 [this](const PlaybackTrace::Visible& v) { return describeTraced(v); });
}

QString Editor::describeTraced(const PlaybackTrace::Visible& v) const {
    const Project& p = project();
    const Clip* clip = p.clip(static_cast<ObjectId>(v.clip));
    const MediaItem* item = clip ? p.findMedia(clip->media) : nullptr;
    QStringList parts;
    QString what = QStringLiteral("%1 · «%2»")
                       .arg(QString::fromStdString(p.trackName(v.track)),
                            item ? QString::fromStdString(item->name) : QStringLiteral("?"));
    if (item && item->still) {
        what += QStringLiteral(" (imagen)");
    }
    if (clip && clip->transform.chromaKey.enabled) {
        what += QStringLiteral(" (pantalla verde)");
    }
    parts << what;
    parts << QStringLiteral("pedido %1, último %2").arg(v.wanted).arg(v.shown);
    const TrackVideo& track = m_video[v.track];
    const int slot = slotWith(v.track, static_cast<ObjectId>(v.clip));
    if (slot < 0) {
        parts << QStringLiteral("ningún hilo colocado para este clip");
    } else {
        const VideoSlot& s = track.decoders[static_cast<std::size_t>(slot)];
        const DecodeWorker::Status st = s.worker->status();
        QString state =
            s.seekIndex >= 0
                ? QStringLiteral("hilo %1 colocado en %2").arg(slot + 1).arg(s.seekIndex)
                : QStringLiteral("hilo %1 ya dando fotogramas").arg(slot + 1);
        if (item && s.path != item->path) {
            state += QStringLiteral(" (proxy)");
        }
        if (!st.opened) {
            state += QStringLiteral(", abriendo el archivo");
        }
        if (st.seekPending >= 0) {
            state += QStringLiteral(", recolocándose en %1").arg(st.seekPending);
        }
        state += st.firstQueued >= 0
                     ? QStringLiteral(", cola %1–%2").arg(st.firstQueued).arg(st.lastQueued)
                     : QStringLiteral(", cola vacía");
        state += QStringLiteral(", siguiente %1").arg(st.nextIndex);
        if (st.endOfStream) {
            state += QStringLiteral(", al final del medio");
        }
        if (st.waiting) {
            state += QStringLiteral(", parado esperando");
        }
        state += QStringLiteral(", caché %1").arg(st.cached);
        state += st.hwAccel.empty()
                     ? QStringLiteral(", por software")
                     : QStringLiteral(", GPU %1").arg(QString::fromStdString(st.hwAccel));
        parts << state;
    }
    // El otro hilo de la pista (el clip siguiente preparado o la otra mitad de una
    // transición).
    for (int i = 0; i < static_cast<int>(track.decoders.size()); ++i) {
        const VideoSlot& s = track.decoders[static_cast<std::size_t>(i)];
        if (i != slot && s.worker) {
            parts
                << QStringLiteral("hilo %1: clip %2 en %3").arg(i + 1).arg(s.clip).arg(s.seekIndex);
        }
    }
    int workers = 0;
    for (const TrackVideo& t : m_video) {
        for (const VideoSlot& s : t.decoders) {
            workers += s.worker ? 1 : 0;
        }
    }
    parts << QStringLiteral("%1 hilos de video abiertos").arg(workers);
    return parts.join(QStringLiteral(" · "));
}

void Editor::advanceShuttle(sharkcut::FrameRate rate) {
    const double fps = static_cast<double>(rate.num) / static_cast<double>(rate.den);
    const double elapsed = static_cast<double>(m_shuttleTimer.elapsed()) / 1000.0;
    qint64 target = m_shuttleFrom + std::llround(elapsed * fps * m_speed);
    const qint64 last = frameCount();
    const bool reachedEnd = target >= last || target <= 0;
    target = std::clamp<qint64>(target, 0, last);
    if (m_speed > 0) {
        // Hacia delante los hilos siguen decodificando en orden (lo que no llegue a
        // tiempo se salta).
        for (std::size_t t : playedVideoTracks()) {
            assignTrack(t, target, false);
            takeTrackFrame(t, target);
        }
        setCurrentFrame(target);
        emit compositionChanged();
    } else {
        // Hacia atrás cada fotograma es un salto: se pide el siguiente cuando llega el
        // anterior (como al arrastrar el cabezal).
        if (m_pendingFrame >= 0 && takeFrames(m_pendingFrame)) {
            m_pendingFrame = -1;
            emit compositionChanged();
        }
        if (m_pendingFrame < 0 && target != m_currentFrame) {
            setCurrentFrame(target);
            requestFrames(target);
            if (!takeFrames(target)) {
                m_pendingFrame = target;
            }
            emit compositionChanged();
        }
    }
    if (reachedEnd) {
        pause();
    }
}

void Editor::setCurrentFrame(qint64 frame) {
    if (m_currentFrame != frame) {
        m_currentFrame = frame;
        emit currentFrameChanged();
        // Propiedades muestra los valores en el cabezal: si el clip está animado, cambian
        // (reproduciendo, como mucho unas veces por segundo).
        for (const ObjectId id : m_selection) {
            const Clip* c = project().clip(id);
            if (c && !c->animation.empty()) {
                if (!m_playing) {
                    m_selectedClipTimer.stop();
                    emit selectedClipChanged();
                } else if (!m_selectedClipTimer.isActive()) {
                    m_selectedClipTimer.start();
                }
                break;
            }
        }
    }
}

void Editor::setPlaying(bool playing) {
    if (m_playing != playing) {
        m_playing = playing;
        emit playingChanged();
    }
}

void Editor::setBusy(bool busy) {
    if (m_busy != busy) {
        m_busy = busy;
        emit busyChanged();
    }
}

void Editor::setError(const QString& message) {
    if (!message.isEmpty()) {
        qWarning("%s", qUtf8Printable(message));
    }
    if (m_errorString != message) {
        m_errorString = message;
        emit errorStringChanged();
    }
}
