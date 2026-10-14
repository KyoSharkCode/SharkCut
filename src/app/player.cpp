#include "player.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>

#include <algorithm>

using sharkcut::DecodeWorker;
using sharkcut::VideoFrame;
using sharkcut::VideoStreamInfo;

namespace {

QString formatFps(const sharkcut::FrameRate& rate) {
    if (rate.den == 1) {
        return QString::number(rate.num);
    }
    return QString::number(static_cast<double>(rate.num) / static_cast<double>(rate.den), 'f', 2);
}

} // namespace

Player::Player(QObject* parent) : QObject(parent) {}

Player::~Player() {
    // Detener el hilo de decodificación antes de destruir el resto de miembros.
    m_worker.reset();
}

QString Player::timecode() const {
    return QString::fromStdString(sharkcut::formatTimecode(m_currentFrame, m_rate));
}

QString Player::durationTimecode() const {
    return QString::fromStdString(sharkcut::formatTimecode(m_frameCount, m_rate));
}

QString Player::decoderText() const {
    if (!m_loaded) {
        return QStringLiteral("—");
    }
    if (m_hwAccelName.isEmpty()) {
        return QStringLiteral("CPU (software)");
    }
    return QStringLiteral("GPU (%1)").arg(m_hwAccelName);
}

void Player::open(const QUrl& url) {
    openPath(url.isLocalFile() ? url.toLocalFile() : url.toString());
}

void Player::openPath(const QString& path) {
    if (path.isEmpty()) {
        return;
    }
    // El hilo anterior se detiene antes de abrir el nuevo archivo.
    m_worker.reset();
    const quint64 session = ++m_session;

    setPlaying(false);
    setError({});
    m_loaded = false;
    m_frame.reset();
    m_frameCount = 0;
    m_pendingFrame = 0; // al abrir se muestra el primer fotograma
    m_hwAccelName.clear();
    m_fileName = QFileInfo(path).fileName();
    setCurrentFrame(0);
    emit loadedChanged();
    emit decoderChanged();
    emit frameChanged();
    setBusy(true);

    // Los avisos llegan desde el hilo de decodificación: se reenvían al hilo de la
    // interfaz y se ignoran si mientras tanto se abrió otro archivo.
    QPointer<Player> self(this);
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

    DecodeWorker::Callbacks callbacks;
    callbacks.opened = [post](const VideoStreamInfo& info) {
        post([info](Player& p) { p.onOpened(info); });
    };
    callbacks.failed = [post](const std::string& message) {
        const QString text = QString::fromStdString(message);
        post([text](Player& p) { p.onFailed(text); });
    };
    callbacks.framesAvailable = [post] { post([](Player& p) { p.onFramesAvailable(); }); };

    // FFmpeg espera la ruta en UTF-8.
    m_worker = std::make_unique<DecodeWorker>(path.toStdString(), sharkcut::HwAccel::Auto,
                                              std::move(callbacks));
}

void Player::onOpened(const VideoStreamInfo& info) {
    m_rate = info.frameRate;
    m_frameCount = std::max<qint64>(info.frameCount, 1);
    m_variableFrameRate = info.variableFrameRate;
    m_frameRateText = formatFps(info.frameRate);
    m_videoDescription = QStringLiteral("%1 · %2×%3")
                             .arg(QString::fromStdString(info.codec).toUpper())
                             .arg(info.width)
                             .arg(info.height);
    m_loaded = true;
    emit loadedChanged();
    emit decoderChanged();
    emit currentFrameChanged(); // el código de tiempo depende de los fps
}

void Player::onFailed(const QString& message) {
    setBusy(false);
    setPlaying(false);
    setError(message);
}

void Player::onFramesAvailable() {
    if (!m_worker) {
        return;
    }
    updateDecoder();
    if (!m_playing && m_pendingFrame >= 0) {
        if (auto frame = m_worker->takeUpTo(m_pendingFrame)) {
            m_pendingFrame = -1;
            setBusy(false);
            showFrame(std::move(frame));
        } else if (m_worker->finished()) {
            m_pendingFrame = -1;
            setBusy(false);
        }
    }
}

void Player::play() {
    if (!m_loaded || m_playing) {
        return;
    }
    if (m_currentFrame >= m_frameCount - 1) {
        seek(0); // al final: empezar de nuevo
    }
    m_pendingFrame = -1;
    m_clockStartFrame = m_currentFrame;
    m_clock.start();
    setPlaying(true);
}

void Player::pause() {
    setPlaying(false);
}

void Player::togglePlay() {
    m_playing ? pause() : play();
}

void Player::seek(qint64 frame) {
    if (!m_loaded || !m_worker) {
        return;
    }
    frame = std::clamp<qint64>(frame, 0, m_frameCount - 1);
    setCurrentFrame(frame);

    if (m_playing) {
        m_worker->seek(frame);
        m_clockStartFrame = frame;
        m_clock.start();
        return;
    }
    // En pausa: si el fotograma ya está preparado (p. ej. al avanzar de uno en uno)
    // se muestra al instante; si no, se pide al hilo de decodificación.
    if (m_worker->hasFrame(frame)) {
        m_pendingFrame = -1;
        showFrame(m_worker->takeUpTo(frame));
        return;
    }
    m_pendingFrame = frame;
    setBusy(true);
    m_worker->seek(frame);
}

void Player::step(int frames) {
    pause();
    seek(m_currentFrame + frames);
}

void Player::advance() {
    if (!m_playing || !m_worker) {
        return;
    }
    const qint64 elapsedUs = m_clock.nsecsElapsed() / 1000;
    const qint64 target = m_clockStartFrame + sharkcut::microsecondsToFrame(elapsedUs, m_rate);

    if (auto frame = m_worker->takeUpTo(target)) {
        showFrame(std::move(frame));
    } else if (m_worker->finished()) {
        pause(); // fin del clip
        return;
    }
    updateDecoder();
}

void Player::showFrame(std::shared_ptr<const VideoFrame> frame) {
    if (!frame) {
        return;
    }
    m_frame = std::move(frame);
    setCurrentFrame(m_frame->index);
    emit frameChanged();
}

void Player::setCurrentFrame(qint64 frame) {
    if (m_currentFrame != frame) {
        m_currentFrame = frame;
        emit currentFrameChanged();
    }
}

void Player::setBusy(bool busy) {
    if (m_busy != busy) {
        m_busy = busy;
        emit busyChanged();
    }
}

void Player::setPlaying(bool playing) {
    if (m_playing != playing) {
        m_playing = playing;
        emit playingChanged();
    }
}

void Player::setError(const QString& message) {
    if (m_errorString != message) {
        m_errorString = message;
        emit errorStringChanged();
    }
}

void Player::updateDecoder() {
    const QString name = QString::fromStdString(m_worker->hwAccelName());
    if (name != m_hwAccelName) {
        m_hwAccelName = name;
        emit decoderChanged();
    }
}
