#include "transitionlab.h"

#include "audio/audio_output.h"
#include "audio/audio_worker.h"
#include "media/sequence_audio.h"
#include "player.h"
#include "render/sequence_exporter.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaObject>
#include <QOffscreenSurface>
#include <QPointer>
#include <rhi/qrhi.h>

#include <algorithm>
#include <cmath>

using sharkcut::DecodeWorker;

TransitionLab::TransitionLab(QObject* parent) : CompositionSource(parent) {
    m_audioOutput = createAudioOutput(nullptr);
}

TransitionLab::~TransitionLab() {
    m_exporter.reset(); // cancela y espera
    m_clock.setAudio(nullptr, nullptr);
    m_audioWorker.reset();
    m_workerA.reset();
    m_workerB.reset();
}

// --- Textos -------------------------------------------------------------------------

QString TransitionLab::rangeText(const Clip& clip) const {
    if (clip.path.isEmpty()) {
        return QStringLiteral("—");
    }
    const auto tc = [&clip](qint64 frame) {
        return QString::fromStdString(sharkcut::formatTimecode(frame, clip.rate));
    };
    const double seconds = static_cast<double>(clip.range.length()) * clip.rate.den /
                           static_cast<double>(clip.rate.num);
    return QStringLiteral("%1 → %2 · %3 s")
        .arg(tc(clip.range.in), tc(clip.range.out - 1))
        .arg(seconds, 0, 'f', 1);
}

QString TransitionLab::clipARange() const {
    return rangeText(m_a);
}

QString TransitionLab::clipBRange() const {
    return rangeText(m_b);
}

QString TransitionLab::timecode() const {
    return QString::fromStdString(sharkcut::formatTimecode(m_currentFrame, m_rate));
}

QString TransitionLab::durationTimecode() const {
    return QString::fromStdString(sharkcut::formatTimecode(m_layout.length(), m_rate));
}

QString TransitionLab::durationText() const {
    const double seconds =
        static_cast<double>(m_params.durationFrames) * m_rate.den / static_cast<double>(m_rate.num);
    return QStringLiteral("%1 fotogramas (%2 s)")
        .arg(m_params.durationFrames)
        .arg(seconds, 0, 'f', 2);
}

QStringList TransitionLab::transitionNames() const {
    QStringList names;
    for (const auto& info : sharkcut::kTransitions) {
        names << QString::fromUtf8(info.name.data(), static_cast<qsizetype>(info.name.size()));
    }
    return names;
}

QStringList TransitionLab::easingNames() const {
    QStringList names;
    for (const auto& info : sharkcut::kEasings) {
        names << QString::fromUtf8(info.name.data(), static_cast<qsizetype>(info.name.size()));
    }
    return names;
}

// --- Parámetros ---------------------------------------------------------------------

int TransitionLab::transitionIndex() const {
    return static_cast<int>(m_params.type);
}

void TransitionLab::setTransitionIndex(int index) {
    if (index < 0 || index >= static_cast<int>(sharkcut::kTransitions.size()) ||
        index == transitionIndex()) {
        return;
    }
    // Cada transición trae sus valores por defecto (como un preset).
    const std::int64_t oldDuration = m_params.durationFrames;
    m_params = sharkcut::kTransitions[static_cast<std::size_t>(index)].defaults;
    emit paramsChanged();
    if (m_params.durationFrames != oldDuration) {
        rebuild();
    }
    emit compositionChanged();
}

void TransitionLab::resetParams() {
    const std::int64_t oldDuration = m_params.durationFrames;
    m_params = sharkcut::transitionInfo(m_params.type).defaults;
    emit paramsChanged();
    if (m_params.durationFrames != oldDuration) {
        rebuild();
    }
    emit compositionChanged();
}

int TransitionLab::easingIndex() const {
    for (std::size_t i = 0; i < sharkcut::kEasings.size(); ++i) {
        if (sharkcut::kEasings[i].easing == m_params.easing) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

void TransitionLab::setEasingIndex(int index) {
    if (index < 0 || index >= static_cast<int>(sharkcut::kEasings.size()) ||
        index == easingIndex()) {
        return;
    }
    m_params.easing = sharkcut::kEasings[static_cast<std::size_t>(index)].easing;
    emit paramsChanged();
    emit compositionChanged();
}

void TransitionLab::setDurationFrames(int frames) {
    frames = std::clamp(frames, 2, 240);
    if (frames == m_params.durationFrames) {
        return;
    }
    m_params.durationFrames = frames;
    emit paramsChanged();
    rebuild();
}

void TransitionLab::setIntensity(double value) {
    value = std::clamp(value, 0.0, 1.0);
    if (std::abs(value - m_params.intensity) < 1e-9) {
        return;
    }
    m_params.intensity = value;
    emit paramsChanged();
    emit compositionChanged();
}

void TransitionLab::setBlur(double value) {
    value = std::clamp(value, 0.0, 1.0);
    if (std::abs(value - m_params.blur) < 1e-9) {
        return;
    }
    m_params.blur = value;
    emit paramsChanged();
    emit compositionChanged();
}

void TransitionLab::setMuted(bool muted) {
    if (m_muted == muted) {
        return;
    }
    m_muted = muted;
    if (m_audioOutput) {
        m_audioOutput->setVolume(muted ? 0.0f : 1.0f);
    }
    emit mutedChanged();
}

// --- Clips ---------------------------------------------------------------------------

void TransitionLab::setClip(Clip& clip, Player* source) {
    if (!source || !source->loaded()) {
        return;
    }
    clip.path = source->filePath();
    clip.name = source->fileName();
    clip.range = {source->rangeIn(), source->rangeOut() + 1};
    clip.rate = source->frameRate();
    clip.size = source->canvasSize();
    rebuild();
    emit clipsChanged();
}

void TransitionLab::setClipA(Player* source) {
    setClip(m_a, source);
}

void TransitionLab::setClipB(Player* source) {
    setClip(m_b, source);
}

void TransitionLab::swapClips() {
    std::swap(m_a, m_b);
    rebuild();
    emit clipsChanged();
}

void TransitionLab::rebuild() {
    pause();
    m_clock.setAudio(nullptr, nullptr);
    m_audioWorker.reset();
    m_workerA.reset();
    m_workerB.reset();
    m_frameA.reset();
    m_frameB.reset();
    const quint64 session = ++m_session;

    if (!hasClipA() || !hasClipB()) {
        m_layout = {};
        emit layoutChanged();
        emit compositionChanged();
        return;
    }

    m_rate = m_a.rate;
    m_canvasSize = m_a.size;
    if (!(m_a.rate == m_b.rate)) {
        setError(QStringLiteral("Los clips tienen distintos fotogramas por segundo; "
                                "la prueba usa los del clip A."));
    } else {
        setError({});
    }
    m_layout = sharkcut::makeLayout(m_a.range, m_b.range, m_params.durationFrames);

    // Avisos de los hilos → hilo de la interfaz (ignorados si mientras tanto se reconstruyó).
    QPointer<TransitionLab> self(this);
    auto post = [self, session](auto fn) {
        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(
            self.data(),
            [self, session, fn = std::move(fn)]() {
                if (self && self->m_session == session) {
                    fn(*self);
                }
            },
            Qt::QueuedConnection);
    };
    auto makeCallbacks = [post] {
        DecodeWorker::Callbacks callbacks;
        callbacks.failed = [post](const std::string& message) {
            const QString text = QString::fromStdString(message);
            post([text](TransitionLab& lab) { lab.setError(text); });
        };
        callbacks.framesAvailable = [post] {
            post([](TransitionLab& lab) { lab.onFramesAvailable(); });
        };
        return callbacks;
    };
    m_workerA = std::make_unique<DecodeWorker>(m_a.path.toStdString(), sharkcut::HwAccel::Auto,
                                               makeCallbacks());
    m_workerB = std::make_unique<DecodeWorker>(m_b.path.toStdString(), sharkcut::HwAccel::Auto,
                                               makeCallbacks());

    if (m_audioOutput) {
        const sharkcut::SequenceLayout layout = m_layout;
        const sharkcut::FrameRate rate = m_rate;
        const std::string pathA = m_a.path.toStdString();
        const std::string pathB = m_b.path.toStdString();
        sharkcut::AudioWorker::Callbacks audioCallbacks;
        audioCallbacks.opened = [post](const sharkcut::AudioStreamInfo&) {
            post([](TransitionLab& lab) {
                lab.m_clock.setAudio(lab.m_audioOutput.get(), lab.m_audioWorker.get());
            });
        };
        m_audioWorker = std::make_unique<sharkcut::AudioWorker>(
            [layout, rate, pathA, pathB] {
                return std::make_unique<sharkcut::SequenceAudioSource>(layout, rate, pathA, pathB);
            },
            *m_audioOutput, std::move(audioCallbacks));
    }

    emit layoutChanged();
    seek(std::min<qint64>(m_currentFrame, std::max<qint64>(m_layout.length() - 1, 0)));
}

// --- Reproducción --------------------------------------------------------------------

void TransitionLab::requestFrames(qint64 frame) {
    const auto at = m_layout.at(frame);
    // A: el fotograma que toca o, si ya terminó, el último de su tramo.
    const qint64 aTarget = at.aFrame ? *at.aFrame : m_layout.a.out - 1;
    // B: el que toca o, si aún no empieza, su primer fotograma (preparado de antemano).
    const qint64 bTarget = at.bFrame ? *at.bFrame : m_layout.b.in;
    if (m_playing || !m_workerA->hasFrame(aTarget)) {
        m_workerA->seek(aTarget);
    }
    if (m_playing || !m_workerB->hasFrame(bTarget)) {
        m_workerB->seek(bTarget);
    }
}

bool TransitionLab::takeFrames(qint64 frame) {
    const auto at = m_layout.at(frame);
    bool complete = true;
    if (at.aFrame) {
        if (auto f = m_workerA->takeUpTo(*at.aFrame)) {
            m_frameA = std::move(f);
        }
        complete = complete && m_frameA && m_frameA->index == *at.aFrame;
    }
    if (at.bFrame) {
        if (auto f = m_workerB->takeUpTo(*at.bFrame)) {
            m_frameB = std::move(f);
        }
        complete = complete && m_frameB && m_frameB->index == *at.bFrame;
    }
    return complete;
}

sharkcut::CompositorInput TransitionLab::composition() const {
    sharkcut::CompositorInput input;
    if (!ready()) {
        return input;
    }
    const auto at = m_layout.at(m_currentFrame);
    if (at.transitionFrame && m_frameA && m_frameB) {
        input.a = m_frameA;
        input.b = m_frameB;
        input.transition = m_params;
        input.state = sharkcut::evaluateTransition(m_params, *at.transitionFrame);
    } else if (at.aFrame) {
        input.a = m_frameA;
    } else {
        input.a = m_frameB;
    }
    return input;
}

void TransitionLab::onFramesAvailable() {
    if (m_playing || m_pendingFrame < 0 || !m_workerA) {
        return;
    }
    if (takeFrames(m_pendingFrame)) {
        m_pendingFrame = -1;
        setBusy(false);
    }
    emit compositionChanged();
}

void TransitionLab::play() {
    if (!ready() || m_playing) {
        return;
    }
    if (m_currentFrame >= m_layout.length() - 1) {
        seek(0);
    }
    m_pendingFrame = -1;
    m_stopAt = -1;
    setBusy(false);
    setPlaying(true);
    m_clock.start(sharkcut::frameToMicroseconds(m_currentFrame, m_rate));
}

void TransitionLab::pause() {
    m_clock.stop();
    m_stopAt = -1;
    setPlaying(false);
}

void TransitionLab::togglePlay() {
    m_playing ? pause() : play();
}

void TransitionLab::seek(qint64 frame) {
    if (!ready() || !m_workerA) {
        return;
    }
    frame = std::clamp<qint64>(frame, 0, m_layout.length() - 1);
    setCurrentFrame(frame);
    requestFrames(frame);
    if (m_playing) {
        m_clock.start(sharkcut::frameToMicroseconds(frame, m_rate));
        return;
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

void TransitionLab::step(int frames) {
    pause();
    seek(m_currentFrame + frames);
}

void TransitionLab::playTransition() {
    if (!ready()) {
        return;
    }
    const qint64 second = m_rate.nominal();
    const qint64 start = std::max<qint64>(m_layout.transitionStart() - second, 0);
    const qint64 stop = std::min<qint64>(m_layout.transitionEnd() + second, m_layout.length() - 1);
    pause();
    seek(start);
    play();
    m_stopAt = stop;
}

void TransitionLab::advance() {
    if (!m_playing || !m_workerA) {
        return;
    }
    qint64 target = sharkcut::microsecondsToFrame(m_clock.nowUs(), m_rate);
    const qint64 last = m_layout.length() - 1;
    const qint64 limit = m_stopAt >= 0 ? m_stopAt : last;
    const bool reachedEnd = target >= limit;
    target = std::min(target, limit);

    takeFrames(target);
    setCurrentFrame(target);
    emit compositionChanged();
    if (reachedEnd) {
        pause();
    }
}

void TransitionLab::setCurrentFrame(qint64 frame) {
    if (m_currentFrame != frame) {
        m_currentFrame = frame;
        emit currentFrameChanged();
    }
}

void TransitionLab::setPlaying(bool playing) {
    if (m_playing != playing) {
        m_playing = playing;
        emit playingChanged();
    }
}

void TransitionLab::setBusy(bool busy) {
    if (m_busy != busy) {
        m_busy = busy;
        emit busyChanged();
    }
}

void TransitionLab::setError(const QString& message) {
    if (m_errorString != message) {
        m_errorString = message;
        emit errorStringChanged();
    }
}

// --- Exportación ----------------------------------------------------------------------

void TransitionLab::exportTo(const QUrl& file) {
    if (!ready() || m_exporter) {
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
    job.content = sharkcut::TransitionTest{m_layout, m_a.path.toStdString(), m_b.path.toStdString(),
                                           m_params};
    job.rate = m_rate;
    job.canvasSize = m_canvasSize;
    job.outputPath = path.toStdString();
#ifndef Q_OS_WIN
    // OpenGL necesita una superficie creada en el hilo de la interfaz.
    if (!m_exportSurface) {
        m_exportSurface.reset(QRhiGles2InitParams::newFallbackSurface());
    }
    job.fallbackSurface = m_exportSurface.get();
#endif

    const quint64 session = ++m_exportSession;
    QPointer<TransitionLab> self(this);
    auto post = [self, session](auto fn) {
        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(
            self.data(),
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
        post([done, total](TransitionLab& lab) {
            lab.m_exportProgress = static_cast<double>(done) / static_cast<double>(total);
            lab.m_exportMessage =
                QStringLiteral("Exportando… %1 / %2 fotogramas").arg(done).arg(total);
            emit lab.exportChanged();
        });
    };
    callbacks.finished = [post, path, timer](bool ok, const std::string& message,
                                             const std::string& encoder) {
        const QString text = QString::fromStdString(message);
        const QString codec = QString::fromStdString(encoder);
        const double seconds = static_cast<double>(timer->elapsed()) / 1000.0;
        post([ok, text, codec, path, seconds](TransitionLab& lab) {
            // El hilo ya terminó: se puede destruir el exportador.
            lab.m_exporter.reset();
            lab.m_exportProgress = ok ? 1.0 : 0.0;
            lab.m_exportMessage = ok ? QStringLiteral("Exportado en %1 s con %2: %3")
                                           .arg(seconds, 0, 'f', 1)
                                           .arg(codec, QDir::toNativeSeparators(path))
                                     : QStringLiteral("No se exportó: %1").arg(text);
            emit lab.exportChanged();
        });
    };

    m_exportProgress = 0.0;
    m_exportMessage = QStringLiteral("Preparando la exportación…");
    m_exporter = std::make_unique<sharkcut::SequenceExporter>(std::move(job), std::move(callbacks));
    emit exportChanged();
}

void TransitionLab::cancelExport() {
    if (m_exporter) {
        m_exporter->cancel(); // el aviso de "terminado" llega después
    }
}
