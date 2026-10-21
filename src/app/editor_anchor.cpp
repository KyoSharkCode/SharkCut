// Punto de anclaje: un clip sigue algo que se mueve en otro clip de video. Se marca la
// zona en el visor, se sigue en segundo plano (media/motion_tracker.h, con el proxy si
// lo hay) y el recorrido pasa a keyframes de posición (core/motion_anchor.h).

#include "editor.h"

#include "core/edit.h"
#include "core/motion_anchor.h"
#include "media/motion_tracker.h"

#include <QCoreApplication>
#include <QPointer>
#include <QThreadPool>

#include <algorithm>
#include <atomic>
#include <map>

using sharkcut::Clip;
using sharkcut::MediaItem;
using sharkcut::ObjectId;
using sharkcut::Project;

struct Editor::AnchorJob {
    quint64 project = 0;
    ObjectId sequence = 0;
    ObjectId clip = 0;
    ObjectId target = 0;
    std::int64_t from = 0; // fotogramas de la línea de tiempo (incluidos)
    std::int64_t to = 0;
    sharkcut::FrameRate sequenceRate{};
    sharkcut::FrameRate mediaRate{};
    std::int64_t targetIn = 0;
    std::int64_t targetStart = 0;
    std::string path; // el proxy si está listo (mismos fotogramas), si no el original
    sharkcut::TrackBox box;
    std::shared_ptr<std::atomic<bool>> cancelled = std::make_shared<std::atomic<bool>>(false);
    // Resultado (lo escribe el hilo de trabajo antes de avisar).
    std::vector<sharkcut::AnchorSample> samples;
    QString error;

    // Fotograma del video seguido que se ve en el fotograma `frame` de la línea de tiempo.
    std::int64_t mediaFrame(std::int64_t frame) const {
        const std::int64_t us =
            sharkcut::frameToMicroseconds(targetIn + (frame - targetStart), sequenceRate);
        return sharkcut::microsecondsToFrame(us, mediaRate);
    }
};

QVariantList Editor::anchorTargets(qint64 clipId) const {
    const Project& p = project();
    QVariantList list;
    // De arriba abajo: el de más arriba suele ser el que se ve.
    for (std::size_t t = p.tracks.size(); t-- > 0;) {
        const sharkcut::Track& track = p.tracks[t];
        if (track.kind != sharkcut::TrackKind::Video || track.hidden) {
            continue;
        }
        const Clip* clip = p.clipAt(t, m_currentFrame);
        if (!clip || clip->id == static_cast<ObjectId>(clipId)) {
            continue;
        }
        const MediaItem* item = p.findMedia(clip->media);
        if (!item || !item->hasVideo) {
            continue;
        }
        list.append(QVariantMap{{QStringLiteral("id"), static_cast<qint64>(clip->id)},
                                {QStringLiteral("name"), QString::fromStdString(item->name)},
                                {QStringLiteral("track"), QString::fromStdString(p.trackName(t))}});
    }
    return list;
}

void Editor::beginAnchorPick(qint64 clipId, qint64 targetId) {
    if (m_anchor || !project().clip(static_cast<ObjectId>(clipId)) ||
        !project().clip(static_cast<ObjectId>(targetId)) || clipId == targetId) {
        return;
    }
    m_anchorPickClip = static_cast<ObjectId>(clipId);
    m_anchorPickTarget = static_cast<ObjectId>(targetId);
    m_anchorMessage.clear();
    emit anchorChanged();
}

void Editor::cancelAnchorPick() {
    if (m_anchorPickClip != 0) {
        m_anchorPickClip = 0;
        m_anchorPickTarget = 0;
        emit anchorChanged();
    }
}

QVariantMap Editor::anchorTargetBox() const {
    const Clip* target = m_anchorPickTarget ? project().clip(m_anchorPickTarget) : nullptr;
    if (!target) {
        return {};
    }
    const sharkcut::ClipTransform t = sharkcut::transformAt(*target, m_currentFrame);
    const CanvasBox box = canvasBox(*target, t);
    return {{QStringLiteral("left"), box.left},
            {QStringLiteral("top"), box.top},
            {QStringLiteral("width"), box.width},
            {QStringLiteral("height"), box.height},
            {QStringLiteral("rotation"), t.rotation},
            {QStringLiteral("pivotX"), project().settings.width / 2.0 + t.x},
            {QStringLiteral("pivotY"), project().settings.height / 2.0 + t.y}};
}

void Editor::startAnchor(double left, double top, double width, double height) {
    const ObjectId clipId = m_anchorPickClip;
    const ObjectId targetId = m_anchorPickTarget;
    cancelAnchorPick();
    const Project& p = project();
    const Clip* clip = p.clip(clipId);
    const Clip* target = p.clip(targetId);
    const MediaItem* item = target ? p.findMedia(target->media) : nullptr;
    if (!clip || !item || m_anchor) {
        return;
    }
    const std::int64_t from = m_currentFrame;
    const std::int64_t to = std::min(clip->end(), target->end()) - 1;
    if (from < clip->start || from < target->start || from > to) {
        m_anchorMessage =
            QStringLiteral("Pon el cabezal donde se ven los dos clips para marcar la zona");
        emit anchorChanged();
        return;
    }
    // El recuadro del lienzo, en el video seguido (con su colocación en el cabezal).
    const sharkcut::ClipTransform t = sharkcut::transformAt(*target, from);
    double u0 = 1.0;
    double v0 = 1.0;
    double u1 = 0.0;
    double v1 = 0.0;
    for (const auto& [x, y] :
         {std::pair{left, top}, std::pair{left + width, top}, std::pair{left, top + height},
          std::pair{left + width, top + height}}) {
        const sharkcut::CanvasPoint m =
            sharkcut::canvasPointOnMedia(p.settings, item->width, item->height, t, x, y);
        u0 = std::min(u0, m.x);
        v0 = std::min(v0, m.y);
        u1 = std::max(u1, m.x);
        v1 = std::max(v1, m.y);
    }
    u0 = std::clamp(u0, 0.0, 1.0);
    v0 = std::clamp(v0, 0.0, 1.0);
    u1 = std::clamp(u1, 0.0, 1.0);
    v1 = std::clamp(v1, 0.0, 1.0);
    if ((u1 - u0) * item->width < 8.0 || (v1 - v0) * item->height < 8.0) {
        m_anchorMessage = QStringLiteral("El recuadro es demasiado pequeño o queda fuera del "
                                         "clip seguido: márcalo sobre él");
        emit anchorChanged();
        return;
    }

    auto job = std::make_shared<AnchorJob>();
    job->project = m_activeProject;
    job->sequence = activeSequenceId();
    job->clip = clipId;
    job->target = targetId;
    job->from = from;
    job->to = to;
    job->sequenceRate = p.settings.frameRate;
    job->mediaRate = item->frameRate;
    job->targetIn = target->in;
    job->targetStart = target->start;
    const QString proxy = m_proxies.readyPath(QString::fromStdString(item->path));
    job->path = proxy.isEmpty() ? item->path : proxy.toStdString();
    job->box = {u0, v0, u1 - u0, v1 - v0};
    m_anchor = job;
    m_anchorProgress = 0.0;
    m_anchorMessage = QStringLiteral("Siguiendo la zona…");
    emit anchorChanged();
    qInfo("Anclaje: seguir %s de %lld a %lld", job->path.c_str(), static_cast<long long>(from),
          static_cast<long long>(to));

    QPointer<Editor> self(this);
    QThreadPool::globalInstance()->start([self, job] {
        std::map<std::int64_t, sharkcut::TrackPoint> points;
        const std::int64_t first = job->mediaFrame(job->from);
        const std::int64_t last = job->mediaFrame(job->to);
        auto lastPercent = std::make_shared<std::atomic<int>>(-1);
        try {
            sharkcut::trackVideo(
                job->path, first, last, job->box,
                [&](std::int64_t index, const sharkcut::TrackPoint& point) {
                    points[index] = point;
                    const double p = last > first ? static_cast<double>(index - first) /
                                                        static_cast<double>(last - first)
                                                  : 1.0;
                    const int percent = static_cast<int>(p * 100.0);
                    if (lastPercent->exchange(percent) != percent) {
                        QMetaObject::invokeMethod(
                            QCoreApplication::instance(),
                            [self, job, p, percent] {
                                if (self && self->m_anchor == job) {
                                    self->m_anchorProgress = p;
                                    self->m_anchorMessage =
                                        QStringLiteral("Siguiendo la zona… %1 %").arg(percent);
                                    emit self->anchorChanged();
                                }
                            },
                            Qt::QueuedConnection);
                    }
                    return true;
                },
                12, job->cancelled.get());
        } catch (const std::exception& e) {
            job->error = QString::fromStdString(e.what());
        }
        // Un punto por fotograma de la línea de tiempo, hasta donde se perdió.
        for (std::int64_t f = job->from; f <= job->to; ++f) {
            const auto it = points.find(job->mediaFrame(f));
            if (it == points.end()) {
                break;
            }
            job->samples.push_back({f, it->second.x, it->second.y});
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, job] {
                if (self) {
                    self->finishAnchor(job);
                }
            },
            Qt::QueuedConnection);
    });
}

void Editor::finishAnchor(const std::shared_ptr<AnchorJob>& job) {
    if (m_anchor != job) {
        return; // cancelado entretanto
    }
    m_anchor.reset();
    m_anchorProgress = 0.0;
    qInfo("Anclaje: %zu fotogramas seguidos%s", job->samples.size(),
          job->error.isEmpty() ? "" : ", con error");
    if (!job->error.isEmpty() || job->samples.size() < 2) {
        m_anchorMessage =
            job->error.isEmpty()
                ? QStringLiteral("No se pudo seguir la zona: prueba con un recuadro sobre algo "
                                 "con más detalle (bordes, dibujo, contraste)")
                : QStringLiteral("No se pudo seguir la zona: %1").arg(job->error);
        emit anchorChanged();
        return;
    }
    const auto change = [job](Project& p) {
        return sharkcut::anchorClip(p, job->clip, job->target, job->samples);
    };
    const QString label = QStringLiteral("Anclar al movimiento");
    if (m_activeProject == job->project && activeSequenceId() == job->sequence) {
        edit(label, change);
    } else if (OpenProject* open = findProject(job->project)) {
        open->history.apply(label.toStdString(), [&](sharkcut::Document& d) {
            return sharkcut::edit(d, job->sequence, change);
        });
        emit projectsChanged();
        emit modifiedChanged();
    }
    const std::int64_t lastFrame = job->samples.back().frame;
    const QString start = QString::fromStdString(
        sharkcut::formatTimecode(job->samples.front().frame, job->sequenceRate));
    const QString end =
        QString::fromStdString(sharkcut::formatTimecode(lastFrame, job->sequenceRate));
    m_anchorMessage =
        lastFrame < job->to
            ? QStringLiteral("La zona se perdió en %1: el clip la sigue de %2 a %1. Desde ahí "
                             "puedes marcarla otra vez.")
                  .arg(end, start)
            : QStringLiteral("Anclado: sigue la zona de %1 a %2. Los keyframes de posición se "
                             "pueden corregir a mano.")
                  .arg(start, end);
    emit anchorChanged();
}

void Editor::cancelAnchor() {
    if (m_anchor) {
        *m_anchor->cancelled = true;
        m_anchor.reset();
        m_anchorProgress = 0.0;
        m_anchorMessage.clear();
        emit anchorChanged();
    }
}
