// Subtítulos automáticos: descarga del modelo (una vez), transcripción del audio de la
// secuencia en segundo plano y creación de la pista de subtítulos.

#include "editor.h"

#include "core/captions.h"
#include "core/edit.h"
#include "media/timeline_audio.h"
#include "modeldownload.h"
#include "speech/transcriber.h"

#include <QCoreApplication>
#include <QDir>
#include <QPointer>
#include <QStandardPaths>
#include <QThread>
#include <QThreadPool>

#include <atomic>

using sharkcut::ObjectId;
using sharkcut::Project;

struct Editor::Transcription {
    quint64 project = 0;
    ObjectId sequence = 0;
    Project snapshot; // la secuencia al empezar, solo con el audio elegido
    sharkcut::CaptionLayout layout;
    sharkcut::CaptionStyle style;
    std::string language;
    sharkcut::SpeechModel model{};
    QString modelPath;
    std::shared_ptr<std::atomic<bool>> cancelled = std::make_shared<std::atomic<bool>>(false);
    QThread* downloadThread = nullptr;
    ModelDownload* download = nullptr;
};

namespace {

QString modelsFolder() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("modelos"));
}

QString megabytes(double bytes) {
    return QString::number(bytes / 1'000'000.0, 'f', 0);
}

} // namespace

void Editor::checkSpeechModels() {
    // Mirar el disco: fuera del hilo de la interfaz.
    QPointer<Editor> self(this);
    QThreadPool::globalInstance()->start([self] {
        const std::string folder = modelsFolder().toStdString();
        QVariantList models;
        for (const sharkcut::SpeechModel& model :
             {sharkcut::kSpeechModelAccurate, sharkcut::kSpeechModelFast}) {
            models.append(QVariantMap{
                {QStringLiteral("downloaded"), sharkcut::isSpeechModelDownloaded(folder, model)},
                {QStringLiteral("megabytes"), megabytes(static_cast<double>(model.size))}});
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, models] {
                if (self && self->m_speechModels != models) {
                    self->m_speechModels = models;
                    emit self->speechModelsChanged();
                }
            },
            Qt::QueuedConnection);
    });
}

void Editor::setTranscribeState(double progress, const QString& message) {
    m_transcribeProgress = progress;
    m_transcribeMessage = message;
    emit transcriptionChanged();
}

void Editor::createCaptions(const QVariantMap& options) {
    const ObjectId sequence = activeSequenceId();
    if (m_transcription || sequence == 0) {
        return;
    }
    auto job = std::make_shared<Transcription>();
    job->project = m_activeProject;
    job->sequence = sequence;
    job->snapshot = project();
    // Solo la pista elegida (si se eligió una): las demás, en silencio.
    const int chosen = options.value(QStringLiteral("audioTrack"), -1).toInt();
    if (chosen >= 0) {
        for (std::size_t t = 0; t < job->snapshot.tracks.size(); ++t) {
            sharkcut::Track& track = job->snapshot.tracks[t];
            if (track.kind == sharkcut::TrackKind::Audio) {
                track.muted = static_cast<int>(t) != chosen;
            }
        }
    }
    if (!job->snapshot.hasAudibleAudio()) {
        setError(
            QStringLiteral("No hay audio que transcribir: la secuencia no tiene clips de audio "
                           "que suenen"));
        return;
    }
    job->layout = captionLayoutFrom(options);
    job->style = presetStyle(options.value(QStringLiteral("preset")).toString());
    job->language =
        options.value(QStringLiteral("language"), QStringLiteral("es")).toString().toStdString();
    job->model = options.value(QStringLiteral("quality"), 0).toInt() == 1
                     ? sharkcut::kSpeechModelFast
                     : sharkcut::kSpeechModelAccurate;
    job->modelPath = QDir(modelsFolder()).filePath(QString::fromLatin1(job->model.file));
    m_transcription = job;
    qInfo("Subtítulos: transcribir %lld fotogramas (idioma %s, modelo %s, audio %d)",
          static_cast<long long>(job->snapshot.length()), job->language.c_str(), job->model.file,
          chosen);
    setTranscribeState(0.0, QStringLiteral("Comprobando el modelo de transcripción…"));

    // ¿Está ya el modelo? (en otro hilo: nada de disco en el de la interfaz)
    QPointer<Editor> self(this);
    QThreadPool::globalInstance()->start([self, job] {
        const bool present =
            sharkcut::isSpeechModelDownloaded(modelsFolder().toStdString(), job->model);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, job, present] {
                if (!self || self->m_transcription != job) {
                    return;
                }
                if (present) {
                    self->startTranscription(job);
                } else {
                    self->startDownload(job);
                }
            },
            Qt::QueuedConnection);
    });
}

void Editor::startDownload(const std::shared_ptr<Transcription>& job) {
    const QUrl url(QString::fromLatin1(sharkcut::kSpeechModelBaseUrl) +
                   QString::fromLatin1(job->model.file));
    qInfo("Subtítulos: descargando el modelo a %s", qUtf8Printable(job->modelPath));
    auto* thread = new QThread;
    auto* download =
        new ModelDownload(url, job->modelPath, job->model.size, QByteArray(job->model.sha256));
    download->moveToThread(thread);
    job->downloadThread = thread;
    job->download = download;
    const QString total = megabytes(static_cast<double>(job->model.size));
    setTranscribeState(0.0, QStringLiteral("Descargando el modelo de transcripción (solo la "
                                           "primera vez): 0 de %1 MB…")
                                .arg(total));
    connect(thread, &QThread::started, download, &ModelDownload::start);
    connect(download, &ModelDownload::progress, this, [this, job, total](double fraction) {
        if (m_transcription == job) {
            setTranscribeState(fraction, QStringLiteral("Descargando el modelo de transcripción "
                                                        "(solo la primera vez): %1 de %2 MB…")
                                             .arg(megabytes(fraction * job->model.size), total));
        }
    });
    connect(download, &ModelDownload::finished, this,
            [this, job, thread](bool ok, const QString& error) {
                thread->quit();
                job->download = nullptr;
                job->downloadThread = nullptr;
                if (m_transcription != job) {
                    return;
                }
                if (ok) {
                    startTranscription(job);
                } else {
                    finishTranscription(job, {}, *job->cancelled ? QString() : error);
                }
            });
    connect(thread, &QThread::finished, download, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void Editor::startTranscription(const std::shared_ptr<Transcription>& job) {
    qInfo("Subtítulos: transcribiendo con %s", qUtf8Printable(job->modelPath));
    setTranscribeState(0.0, QStringLiteral("Transcribiendo el audio…"));
    checkSpeechModels(); // si se acaba de descargar, el diálogo ya lo dice
    QPointer<Editor> self(this);
    QThreadPool::globalInstance()->start([self, job] {
        std::vector<sharkcut::TimedWord> words;
        QString error;
        try {
            sharkcut::TimelineAudioSource source(job->snapshot);
            const std::int64_t samples = sharkcut::rescale(
                job->snapshot.length(), job->snapshot.settings.frameRate.frameDuration(),
                sharkcut::Rational{1, 48'000});
            auto lastPercent = std::make_shared<std::atomic<int>>(-1);
            sharkcut::TranscribeOptions options;
            options.language = job->language;
            options.cancelled = [flag = job->cancelled] { return flag->load(); };
            options.progress = [self, job, lastPercent](double p) {
                const int percent = static_cast<int>(p * 100.0);
                if (lastPercent->exchange(percent) == percent) {
                    return; // sin cambios a la vista: no se molesta a la interfaz
                }
                QMetaObject::invokeMethod(
                    QCoreApplication::instance(),
                    [self, job, p, percent] {
                        if (self && self->m_transcription == job) {
                            self->setTranscribeState(
                                p, QStringLiteral("Transcribiendo el audio… %1 %").arg(percent));
                        }
                    },
                    Qt::QueuedConnection);
            };
            words = sharkcut::transcribe(job->modelPath.toStdString(), source, samples, options);
        } catch (const std::exception& e) {
            error = QString::fromStdString(e.what());
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, job, words = std::move(words), error]() mutable {
                if (self) {
                    self->finishTranscription(job, std::move(words), error);
                }
            },
            Qt::QueuedConnection);
    });
}

void Editor::finishTranscription(const std::shared_ptr<Transcription>& job,
                                 std::vector<sharkcut::TimedWord> words, const QString& error) {
    if (m_transcription != job) {
        return; // se canceló (o se sustituyó) entretanto
    }
    m_transcription.reset();
    setTranscribeState(0.0, {});
    qInfo("Subtítulos: transcripción terminada (%zu palabras%s%s)", words.size(),
          *job->cancelled ? ", cancelada" : "", error.isEmpty() ? "" : ", con error");
    if (*job->cancelled) {
        return;
    }
    if (!error.isEmpty()) {
        setError(QStringLiteral("No se pudieron crear los subtítulos: %1").arg(error));
        return;
    }
    std::vector<sharkcut::Caption> captions =
        sharkcut::layoutCaptions(words, job->layout, job->snapshot.settings.frameRate);
    sharkcut::limitCaptionsTo(captions, sharkcut::mediaEnd(job->snapshot));
    if (captions.empty()) {
        setError(QStringLiteral("No se encontró voz en el audio de la secuencia"));
        return;
    }
    const sharkcut::CaptionStyle style = job->style;
    const auto change = [&captions, &style](Project& p) {
        const std::size_t track = sharkcut::addCaptionTrack(p, style);
        sharkcut::setCaptions(p, track, captions);
        return true;
    };
    const QString label = QStringLiteral("Crear subtítulos");
    if (m_activeProject == job->project && activeSequenceId() == job->sequence) {
        edit(label, change);
    } else if (OpenProject* open = findProject(job->project)) {
        // Se cambió de pestaña mientras tanto: van a la secuencia que se transcribió.
        open->history.apply(label.toStdString(), [&](sharkcut::Document& d) {
            return sharkcut::edit(d, job->sequence, change);
        });
        emit projectsChanged();
        emit modifiedChanged();
    }
    emit captionsCreated(static_cast<int>(captions.size()));
}

void Editor::cancelTranscription() {
    const std::shared_ptr<Transcription> job = m_transcription;
    if (!job) {
        return;
    }
    *job->cancelled = true;
    if (job->download) {
        job->download->cancel();
    }
    m_transcription.reset();
    setTranscribeState(0.0, {});
}

void Editor::shutdownTranscription() {
    const std::shared_ptr<Transcription> job = m_transcription;
    if (!job) {
        return;
    }
    QThread* download = job->downloadThread;
    cancelTranscription(); // la transcripción en curso termina enseguida
    if (download) {
        download->quit();
        download->wait();
    }
}
