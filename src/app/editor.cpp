#include "editor.h"

#include "audio/audio_output.h"
#include "audio/audio_worker.h"
#include "core/edit.h"
#include "core/project_file.h"
#include "media/media_error.h"
#include "media/media_info.h"
#include "media/timeline_audio.h"
#include "player.h"
#include "render/sequence_exporter.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaObject>
#include <QOffscreenSurface>
#include <QPointer>
#include <QStandardPaths>
#include <QThreadPool>
#include <QVariantMap>
#include <rhi/qrhi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <filesystem>
#include <numeric>

using sharkcut::Clip;
using sharkcut::DecodeWorker;
using sharkcut::MediaItem;
using sharkcut::ObjectId;
using sharkcut::Project;

namespace {

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
    if (project.length() > 0 || !item || !item->hasVideo || item->width <= 0 || item->height <= 0) {
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

} // namespace

Editor::Editor(QObject* parent) : CompositionSource(parent) {
    m_audioOutput = createAudioOutput(nullptr);
}

Editor::~Editor() {
    m_exporter.reset(); // cancela y espera
    m_clock.setAudio(nullptr, nullptr);
    m_audioWorker.reset();
    for (VideoSlot& slot : m_slots) {
        slot.worker.reset();
    }
}

// --- Propiedades ------------------------------------------------------------------

QVariantList Editor::mediaItems() const {
    QVariantList list;
    for (const MediaItem& item : project().media) {
        QVariantMap entry;
        entry[QStringLiteral("id")] = static_cast<qint64>(item.id);
        entry[QStringLiteral("name")] = QString::fromStdString(item.name);
        entry[QStringLiteral("path")] = QString::fromStdString(item.path);
        entry[QStringLiteral("duration")] = durationText(item.durationUs);
        entry[QStringLiteral("video")] = item.hasVideo;
        entry[QStringLiteral("audio")] = item.hasAudio;
        entry[QStringLiteral("thumbnail")] =
            item.hasVideo ? QString(QStringLiteral("image://miniaturas/") +
                                    QString::fromLatin1(
                                        QUrl::toPercentEncoding(QString::fromStdString(item.path))))
                          : QString();
        entry[QStringLiteral("details")] = item.hasVideo ? QStringLiteral("%1×%2 · %3 fps")
                                                               .arg(item.width)
                                                               .arg(item.height)
                                                               .arg(fpsText(item.frameRate))
                                                         : QStringLiteral("Solo audio");
        list.append(entry);
    }
    return list;
}

QVariantList Editor::clips() const {
    QVariantList list;
    const Project& p = project();
    for (std::size_t t = 0; t < p.tracks.size(); ++t) {
        for (const Clip& clip : p.tracks[t].clips) {
            const MediaItem* item = p.findMedia(clip.media);
            QVariantMap entry;
            entry[QStringLiteral("id")] = static_cast<qint64>(clip.id);
            entry[QStringLiteral("link")] = static_cast<qint64>(clip.link);
            entry[QStringLiteral("track")] = static_cast<int>(t);
            entry[QStringLiteral("start")] = static_cast<qint64>(clip.start);
            entry[QStringLiteral("length")] = static_cast<qint64>(clip.length);
            entry[QStringLiteral("in")] = static_cast<qint64>(clip.in);
            entry[QStringLiteral("name")] =
                item ? QString::fromStdString(item->name) : QStringLiteral("?");
            list.append(entry);
        }
    }
    return list;
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
    return canUndo()
               ? QStringLiteral("Deshacer: %1").arg(QString::fromStdString(m_history.undoLabel()))
               : QStringLiteral("Deshacer");
}

QString Editor::redoText() const {
    return canRedo()
               ? QStringLiteral("Rehacer: %1").arg(QString::fromStdString(m_history.redoLabel()))
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
    const Clip* first = p.clip(group.front());
    const bool single = group.size() == 1 || (group.size() == 2 && first->link == group[1]);
    if (!single) {
        return QStringLiteral("%1 clips").arg(group.size());
    }
    const MediaItem* item = p.findMedia(first->media);
    const auto tc = [this](std::int64_t frame) { return timecodeFor(frame); };
    return QStringLiteral("%1\n%2 → %3 · %4")
        .arg(item ? QString::fromStdString(item->name) : QStringLiteral("?"), tc(first->start),
             tc(first->end()), tc(first->length));
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
    if (project().clipAt(sharkcut::kVideoTrack, m_currentFrame)) {
        input.a = m_frame;
    }
    return input;
}

// --- Archivo del proyecto -------------------------------------------------------------

QString Editor::projectName() const {
    if (!m_projectPath.isEmpty()) {
        return QFileInfo(m_projectPath).completeBaseName();
    }
    return m_newName.isEmpty() ? QStringLiteral("Sin título") : m_newName;
}

QString Editor::canvasLabel() const {
    const int w = project().settings.width;
    const int h = project().settings.height;
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

QUrl Editor::defaultExportFolder() const {
    if (!m_projectPath.isEmpty()) {
        return QUrl::fromLocalFile(QFileInfo(m_projectPath).absolutePath());
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
    pause();
    Project project;
    if (width > 0 && height > 0) {
        project.settings.width = width;
        project.settings.height = height;
        project.settings.adoptCanvas = false;
    }
    if (fps > 0) {
        project.settings.frameRate = {fps, 1};
        project.settings.adoptFrameRate = false;
    }
    m_history.reset(std::move(project));
    m_projectPath.clear();
    m_newName = name.trimmed();
    m_selection.clear();
    setCurrentFrame(0);
    setError({});
    onProjectChanged();
    emit fileChanged();
}

void Editor::openProject(const QUrl& file) {
    openProjectPath(file.isLocalFile() ? file.toLocalFile() : file.toString());
}

void Editor::openProjectPath(const QString& path) {
    if (path.isEmpty() || m_fileBusy) {
        return;
    }
    pause();
    setFileBusy(true);
    QPointer<Editor> self(this);
    QThreadPool::globalInstance()->start([self, path] {
        auto project = std::make_shared<Project>();
        QString error;
        QStringList missing;
        try {
            *project = sharkcut::loadProject(sharkcut::pathFromUtf8(path.toStdString()));
            for (const MediaItem& item : project->media) {
                std::error_code ignored;
                if (!std::filesystem::exists(sharkcut::pathFromUtf8(item.path), ignored)) {
                    missing << QString::fromStdString(item.name);
                }
            }
        } catch (const std::exception& e) {
            error = QString::fromStdString(e.what());
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, project, path, error, missing] {
                if (self) {
                    self->finishOpen(project, path, error, missing);
                }
            },
            Qt::QueuedConnection);
    });
}

void Editor::finishOpen(const std::shared_ptr<Project>& project, const QString& path,
                        const QString& error, const QStringList& missing) {
    setFileBusy(false);
    if (!error.isEmpty()) {
        setError(QStringLiteral("No se pudo abrir el proyecto: %1").arg(error));
        emit opened(false);
        return;
    }
    m_history.reset(std::move(*project));
    m_projectPath = path;
    m_selection.clear();
    setCurrentFrame(0);
    setError(missing.isEmpty() ? QString()
                               : QStringLiteral("No se encuentran estos medios: %1")
                                     .arg(missing.join(QStringLiteral(", "))));
    onProjectChanged();
    emit fileChanged();
    emit opened(true);
}

bool Editor::save() {
    if (m_projectPath.isEmpty()) {
        return false;
    }
    saveToPath(m_projectPath);
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
    saveToPath(path);
}

void Editor::saveToPath(const QString& path) {
    if (m_fileBusy) {
        return;
    }
    setFileBusy(true);
    const std::shared_ptr<const Project> snapshot = m_history.snapshot();
    QPointer<Editor> self(this);
    QThreadPool::globalInstance()->start([self, snapshot, path] {
        QString error;
        try {
            sharkcut::saveProject(*snapshot, sharkcut::pathFromUtf8(path.toStdString()));
        } catch (const std::exception& e) {
            error = QString::fromStdString(e.what());
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, snapshot, path, error] {
                if (self) {
                    self->finishSave(snapshot, path, error);
                }
            },
            Qt::QueuedConnection);
    });
}

void Editor::finishSave(const std::shared_ptr<const Project>& written, const QString& path,
                        const QString& error) {
    setFileBusy(false);
    if (!error.isEmpty()) {
        setError(error);
        emit saved(false);
        return;
    }
    m_history.markSaved(written);
    if (m_projectPath != path) {
        m_projectPath = path;
        emit fileChanged();
    }
    emit modifiedChanged();
    emit saved(true);
}

// --- Exportación ----------------------------------------------------------------------

void Editor::exportToFolder(const QUrl& folder, const QString& name, int quality) {
    const QString directory = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
    const QString file = name.trimmed();
    if (directory.isEmpty() || file.isEmpty()) {
        return;
    }
    exportTo(QUrl::fromLocalFile(QDir(directory).filePath(file)), quality);
}

void Editor::exportTo(const QUrl& file, int quality) {
    if (frameCount() <= 0 || m_exporter) {
        return;
    }
    QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (path.isEmpty()) {
        return;
    }
    if (!path.endsWith(QLatin1String(".mp4"), Qt::CaseInsensitive)) {
        path += QLatin1String(".mp4");
    }
    pause();

    sharkcut::ExportJob job;
    job.content = project();
    job.rate = project().settings.frameRate;
    job.canvasSize = canvasSize();
    job.outputPath = path.toStdString();
    constexpr std::array<std::int64_t, 3> kBitrates{8'000'000, 16'000'000, 40'000'000};
    job.videoBitrate = kBitrates[static_cast<std::size_t>(std::clamp(quality, 0, 2))];
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
    callbacks.finished = [post, path, timer](bool ok, const std::string& message,
                                             const std::string& encoder) {
        const QString text = QString::fromStdString(message);
        const QString codec = QString::fromStdString(encoder);
        const double seconds = static_cast<double>(timer->elapsed()) / 1000.0;
        post([ok, text, codec, path, seconds](Editor& editor) {
            editor.m_exporter.reset(); // el hilo ya terminó
            editor.m_exportProgress = ok ? 1.0 : 0.0;
            editor.m_exportMessage = ok ? QStringLiteral("Exportado en %1 s con %2: %3")
                                              .arg(seconds, 0, 'f', 1)
                                              .arg(codec, QDir::toNativeSeparators(path))
                                        : QStringLiteral("No se exportó: %1").arg(text);
            emit editor.exportChanged();
        });
    };

    m_exportProgress = 0.0;
    m_exportMessage = QStringLiteral("Preparando la exportación…");
    m_exporter = std::make_unique<sharkcut::SequenceExporter>(std::move(job), std::move(callbacks));
    emit exportChanged();
}

void Editor::cancelExport() {
    if (m_exporter) {
        m_exporter->cancel(); // el aviso de "terminado" llega después
    }
}

// --- Biblioteca ---------------------------------------------------------------------

void Editor::importFiles(const QList<QUrl>& urls, qint64 placeAt) {
    QStringList paths;
    for (const QUrl& url : urls) {
        const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
        if (!path.isEmpty()) {
            paths << path;
        }
    }
    importPaths(paths, placeAt);
}

void Editor::importPaths(const QStringList& paths, qint64 placeAt) {
    if (paths.isEmpty()) {
        return;
    }
    ++m_importing;
    emit importingChanged();

    // El análisis abre los archivos: fuera del hilo de la interfaz.
    QPointer<Editor> self(this);
    QThreadPool::globalInstance()->start([self, paths, placeAt] {
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
                if (info.video) {
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
            [self, items, errors, placeAt] {
                if (self) {
                    self->finishImport(std::move(*items), errors, placeAt);
                }
            },
            Qt::QueuedConnection);
    });
}

void Editor::finishImport(std::vector<MediaItem> items, QStringList errors, qint64 placeAt) {
    --m_importing;
    emit importingChanged();
    if (!errors.isEmpty()) {
        setError(QStringLiteral("No se pudo importar: %1").arg(errors.join(QStringLiteral("; "))));
    }
    if (items.empty()) {
        return;
    }
    std::int64_t end = -1;
    const QString label =
        placeAt >= 0 ? QStringLiteral("Añadir a la línea de tiempo") : QStringLiteral("Importar");
    edit(label, [&](Project& p) {
        bool changed = false;
        std::int64_t at = placeAt;
        for (const MediaItem& item : items) {
            ObjectId id = findMediaByPath(p, item.path);
            if (id == 0) {
                id = sharkcut::addMedia(p, item);
                changed = true;
            }
            if (placeAt >= 0) {
                adoptSettings(p, id);
                if (const ObjectId clip = sharkcut::placeClip(p, id, at)) {
                    at = p.clip(clip)->end();
                    end = at;
                    changed = true;
                }
            }
        }
        return changed;
    });
    if (end >= 0) {
        seek(end);
    }
}

void Editor::removeMedia(qint64 mediaId) {
    edit(QStringLiteral("Quitar medio"), [mediaId](Project& p) {
        return sharkcut::removeMedia(p, static_cast<ObjectId>(mediaId));
    });
}

QString Editor::mediaPath(qint64 mediaId) const {
    const MediaItem* item = project().findMedia(static_cast<ObjectId>(mediaId));
    return item ? QString::fromStdString(item->path) : QString();
}

// --- Edición ------------------------------------------------------------------------

bool Editor::edit(const QString& label, const std::function<bool(Project&)>& change) {
    if (!m_history.apply(label.toStdString(), change)) {
        return false;
    }
    onProjectChanged();
    return true;
}

void Editor::addMedia(qint64 mediaId, qint64 frame) {
    std::int64_t end = -1;
    const bool placed = edit(QStringLiteral("Añadir clip"), [&](Project& p) {
        const auto id = static_cast<ObjectId>(mediaId);
        adoptSettings(p, id);
        const ObjectId clip = sharkcut::placeClip(p, id, frame < 0 ? p.length() : frame);
        if (clip != 0) {
            end = p.clip(clip)->end();
        }
        return clip != 0;
    });
    if (placed) {
        seek(end);
    }
}

void Editor::addFromSource(Player* source, bool atPlayhead) {
    if (!source || !source->loaded()) {
        return;
    }
    const std::string path = source->filePath().toStdString();
    const sharkcut::FrameRate sourceRate = source->frameRate();
    MediaItem item;
    item.path = path;
    item.name = source->fileName().toStdString();
    item.durationUs = sharkcut::frameToMicroseconds(source->frameCount(), sourceRate);
    item.frameRate = sourceRate;
    item.width = source->canvasSize().width();
    item.height = source->canvasSize().height();
    item.hasVideo = true;
    item.hasAudio = source->hasAudio();
    // Tramo marcado en µs del medio (la salida del reproductor es el último incluido).
    const std::int64_t inUs = sharkcut::frameToMicroseconds(source->rangeIn(), sourceRate);
    const std::int64_t outUs = sharkcut::frameToMicroseconds(source->rangeOut() + 1, sourceRate);
    const std::int64_t playhead = m_currentFrame;

    std::int64_t end = -1;
    const bool placed = edit(QStringLiteral("Añadir tramo"), [&](Project& p) {
        ObjectId id = findMediaByPath(p, path);
        if (id == 0) {
            id = sharkcut::addMedia(p, item);
        }
        adoptSettings(p, id);
        const sharkcut::Rational frame = p.settings.frameRate.frameDuration();
        const std::int64_t in = sharkcut::rescale(inUs, sharkcut::kMicroseconds, frame);
        const std::int64_t out = sharkcut::rescale(outUs, sharkcut::kMicroseconds, frame);
        const std::int64_t start = atPlayhead ? playhead : p.length();
        const ObjectId clip = sharkcut::placeClip(p, id, start, in, out - in);
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
    edit(QStringLiteral("Dividir"), [frame](Project& p) { return sharkcut::splitAt(p, frame); });
}

void Editor::splitClip(qint64 clipId, qint64 frame) {
    edit(QStringLiteral("Dividir"), [clipId, frame](Project& p) {
        return sharkcut::splitAt(p, frame, {static_cast<ObjectId>(clipId)});
    });
}

void Editor::removeSelection() {
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

void Editor::moveClips(qint64 clipId, qint64 delta) {
    const auto id = static_cast<ObjectId>(clipId);
    const std::vector<ObjectId> ids = isSelected(clipId) ? m_selection : std::vector<ObjectId>{id};
    edit(QStringLiteral("Mover"),
         [&ids, delta](Project& p) { return sharkcut::moveClips(p, ids, delta); });
}

qint64 Editor::limitMove(qint64 clipId, qint64 delta) const {
    const auto id = static_cast<ObjectId>(clipId);
    const std::vector<ObjectId> ids = isSelected(clipId) ? m_selection : std::vector<ObjectId>{id};
    return sharkcut::limitMove(project(), ids, delta);
}

void Editor::undo() {
    if (m_history.undo()) {
        onProjectChanged();
    }
}

void Editor::redo() {
    if (m_history.redo()) {
        onProjectChanged();
    }
}

void Editor::select(qint64 clipId, bool add) {
    const auto id = static_cast<ObjectId>(clipId);
    if (!project().clip(id)) {
        return;
    }
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
    if (!m_selection.empty()) {
        m_selection.clear();
        emit selectionChanged();
    }
}

bool Editor::isSelected(qint64 clipId) const {
    const auto id = static_cast<ObjectId>(clipId);
    const std::vector<ObjectId> group = sharkcut::withLinked(project(), m_selection);
    return std::find(group.begin(), group.end(), id) != group.end();
}

void Editor::onProjectChanged() {
    // La selección solo conserva los clips que siguen existiendo; su descripción
    // puede cambiar aunque sigan (recortes, movimientos).
    std::erase_if(m_selection, [this](ObjectId id) { return project().clip(id) == nullptr; });
    emit selectionChanged();

    const bool wasPlaying = m_playing;
    pause();
    resetVideoSlots();
    restartAudio();
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
    if (!m_audioOutput || project().tracks[sharkcut::kAudioTrack].clips.empty()) {
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
    for (VideoSlot& slot : m_slots) {
        if (slot.worker && !project().findMedia(slot.media)) {
            slot.worker.reset();
            slot.media = 0;
        }
        slot.clip = 0;
        slot.seekIndex = -1;
    }
    m_current = -1;
}

std::int64_t Editor::sourceIndex(const Clip& clip, std::int64_t frame) const {
    const MediaItem* item = project().findMedia(clip.media);
    if (!item) {
        return 0;
    }
    return sharkcut::microsecondsToFrame(project().mediaTimeUs(clip, frame), item->frameRate);
}

int Editor::slotFor(const Clip& clip, int avoid) {
    for (int i = 0; i < static_cast<int>(m_slots.size()); ++i) {
        if (i != avoid && m_slots[i].worker && m_slots[i].clip == clip.id) {
            return i;
        }
    }
    // Uno libre, mejor si ya tiene abierto el mismo medio.
    int choice = -1;
    for (int i = 0; i < static_cast<int>(m_slots.size()); ++i) {
        if (i != avoid && m_slots[i].worker && m_slots[i].media == clip.media) {
            choice = i;
            break;
        }
    }
    if (choice < 0) {
        choice = avoid == 0 ? 1 : 0;
    }
    VideoSlot& slot = m_slots[static_cast<std::size_t>(choice)];
    if (!slot.worker || slot.media != clip.media) {
        slot.worker.reset();
        const MediaItem* item = project().findMedia(clip.media);
        DecodeWorker::Callbacks callbacks;
        callbacks.failed = [this](const std::string& message) {
            const QString text = QString::fromStdString(message);
            QMetaObject::invokeMethod(this, [this, text] { setError(text); }, Qt::QueuedConnection);
        };
        callbacks.framesAvailable = [this] {
            QMetaObject::invokeMethod(this, [this] { onFramesAvailable(); }, Qt::QueuedConnection);
        };
        slot.worker = std::make_unique<DecodeWorker>(item ? item->path : std::string(),
                                                     sharkcut::HwAccel::Auto, std::move(callbacks));
        slot.media = clip.media;
    }
    slot.clip = clip.id;
    slot.seekIndex = -1;
    return choice;
}

void Editor::placeSlot(int slot, const Clip& clip, std::int64_t frame, bool always) {
    VideoSlot& s = m_slots[static_cast<std::size_t>(slot)];
    const std::int64_t index = sourceIndex(clip, frame);
    if (always || !s.worker->hasFrame(index)) {
        s.worker->seek(index);
    }
    s.seekIndex = index;
}

const Clip* Editor::nextVideoClip(std::int64_t frame) const {
    const auto& clips = project().tracks[sharkcut::kVideoTrack].clips;
    const auto it = std::upper_bound(clips.begin(), clips.end(), frame,
                                     [](std::int64_t f, const Clip& c) { return f < c.start; });
    return it != clips.end() ? &*it : nullptr;
}

void Editor::prepareNext(std::int64_t frame) {
    const Clip* next = nextVideoClip(frame);
    if (!next) {
        return;
    }
    const int slot = slotFor(*next, m_current);
    VideoSlot& s = m_slots[static_cast<std::size_t>(slot)];
    const std::int64_t index = sourceIndex(*next, next->start);
    if (s.seekIndex != index) {
        s.worker->seek(index); // su primer fotograma, preparado de antemano
        s.seekIndex = index;
    }
}

void Editor::requestFrames(qint64 frame) {
    const Clip* current = project().clipAt(sharkcut::kVideoTrack, frame);
    if (current) {
        const bool assigned =
            m_current >= 0 && m_slots[static_cast<std::size_t>(m_current)].clip == current->id;
        const int slot = assigned ? m_current : slotFor(*current, -1);
        m_current = slot;
        placeSlot(slot, *current, frame, m_playing || !assigned);
    } else {
        m_current = -1;
    }
    prepareNext(frame);
}

bool Editor::takeFrames(qint64 frame) {
    const Clip* current = project().clipAt(sharkcut::kVideoTrack, frame);
    if (!current) {
        m_frame.reset(); // hueco: negro
        m_frameClip = 0;
        return true;
    }
    if (m_current < 0 || m_slots[static_cast<std::size_t>(m_current)].clip != current->id) {
        return false;
    }
    DecodeWorker& worker = *m_slots[static_cast<std::size_t>(m_current)].worker;
    const std::int64_t index = sourceIndex(*current, frame);
    if (auto f = worker.takeUpTo(index)) {
        m_frame = std::move(f);
        m_frameClip = current->id;
    }
    const bool exact = m_frame && m_frameClip == current->id && m_frame->index == index;
    return exact || worker.finished();
}

void Editor::onFramesAvailable() {
    if (m_playing || m_pendingFrame < 0) {
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
    if (m_currentFrame >= frameCount() - 1) {
        seek(0);
    }
    m_pendingFrame = -1;
    setBusy(false);
    setPlaying(true);
    m_clock.start(sharkcut::frameToMicroseconds(m_currentFrame, project().settings.frameRate));
}

void Editor::pause() {
    m_clock.stop();
    setPlaying(false);
}

void Editor::togglePlay() {
    m_playing ? pause() : play();
}

void Editor::seek(qint64 frame) {
    const qint64 total = frameCount();
    if (total <= 0) {
        setCurrentFrame(0);
        m_frame.reset();
        m_frameClip = 0;
        m_pendingFrame = -1;
        setBusy(false);
        emit compositionChanged();
        return;
    }
    frame = std::clamp<qint64>(frame, 0, total - 1);
    setCurrentFrame(frame);
    requestFrames(frame);
    if (m_playing) {
        m_clock.start(sharkcut::frameToMicroseconds(frame, project().settings.frameRate));
        return;
    }
    // En pausa: se muestra en cuanto llegue el fotograma de ese instante.
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
    qint64 target = sharkcut::microsecondsToFrame(m_clock.nowUs(), rate);
    const qint64 last = frameCount() - 1;
    const bool reachedEnd = target >= last;
    target = std::min(target, last);

    // Al entrar en otro clip se usa el hilo que ya estaba preparado.
    if (const Clip* current = project().clipAt(sharkcut::kVideoTrack, target)) {
        if (m_current < 0 || m_slots[static_cast<std::size_t>(m_current)].clip != current->id) {
            int slot = -1;
            for (int i = 0; i < static_cast<int>(m_slots.size()); ++i) {
                if (m_slots[static_cast<std::size_t>(i)].worker &&
                    m_slots[static_cast<std::size_t>(i)].clip == current->id) {
                    slot = i;
                }
            }
            if (slot < 0) {
                slot = slotFor(*current, -1);
                placeSlot(slot, *current, target, true);
            }
            m_current = slot;
        }
    }
    prepareNext(target);
    takeFrames(target);
    setCurrentFrame(target);
    emit compositionChanged();
    if (reachedEnd) {
        pause();
    }
}

void Editor::setCurrentFrame(qint64 frame) {
    if (m_currentFrame != frame) {
        m_currentFrame = frame;
        emit currentFrameChanged();
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
    if (m_errorString != message) {
        m_errorString = message;
        emit errorStringChanged();
    }
}
