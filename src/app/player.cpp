#include "player.h"

#include "audio/audio_output.h"
#include "audio/audio_worker.h"
#include "media/audio_decoder.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>

#include <algorithm>
#include <exception>

using sharkcut::AudioStreamInfo;
using sharkcut::AudioWorker;
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

QString channelsText(int channels) {
    switch (channels) {
    case 1:
        return QStringLiteral("mono");
    case 2:
        return QStringLiteral("estéreo");
    default:
        return QStringLiteral("%1 canales").arg(channels);
    }
}

} // namespace

Player::Player(QObject* parent) : CompositionSource(parent) {
    m_audioOutput = createAudioOutput(&m_audioOutputText);
}

Player::~Player() {
    // Detener los hilos antes de destruir el resto de miembros (y la salida de audio).
    m_clock.setAudio(nullptr, nullptr);
    m_audioWorker.reset();
    m_worker.reset();
}

QString Player::timecode() const {
    return QString::fromStdString(sharkcut::formatTimecode(m_currentFrame, m_rate));
}

QString Player::durationTimecode() const {
    return QString::fromStdString(sharkcut::formatTimecode(m_frameCount, m_rate));
}

QString Player::markInTimecode() const {
    return m_markIn >= 0 ? QString::fromStdString(sharkcut::formatTimecode(m_markIn, m_rate))
                         : QStringLiteral("—");
}

QString Player::markOutTimecode() const {
    return m_markOut >= 0 ? QString::fromStdString(sharkcut::formatTimecode(m_markOut, m_rate))
                          : QStringLiteral("—");
}

QString Player::markedDurationTimecode() const {
    const qint64 length = std::max<qint64>(rangeOut() - rangeIn() + 1, 0);
    return QString::fromStdString(sharkcut::formatTimecode(length, m_rate));
}

sharkcut::CompositorInput Player::composition() const {
    sharkcut::CompositorInput input;
    input.a = m_frame;
    return input;
}

QSize Player::canvasSize() const {
    return m_frame ? QSize(m_frame->width, m_frame->height) : QSize(1920, 1080);
}

void Player::setMarkIn() {
    if (!m_loaded) {
        return;
    }
    m_markIn = m_currentFrame;
    if (m_markOut >= 0 && m_markOut < m_markIn) {
        m_markOut = -1; // una salida anterior a la entrada no tiene sentido
    }
    emit marksChanged();
}

void Player::setMarkOut() {
    if (!m_loaded) {
        return;
    }
    m_markOut = m_currentFrame;
    if (m_markIn >= 0 && m_markIn > m_markOut) {
        m_markIn = -1;
    }
    emit marksChanged();
}

void Player::clearMarks() {
    m_markIn = -1;
    m_markOut = -1;
    emit marksChanged();
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

QString Player::audioDescription() const {
    if (!m_loaded) {
        return QStringLiteral("—");
    }
    if (!m_hasAudio) {
        return QStringLiteral("Sin audio");
    }
    return QStringLiteral("%1 · %2 kHz · %3")
        .arg(QString::fromStdString(m_audioInfo.codec).toUpper())
        .arg(m_audioInfo.sampleRate / 1000.0, 0, 'g', 3)
        .arg(channelsText(m_audioInfo.channels));
}

void Player::setMuted(bool muted) {
    if (m_muted == muted) {
        return;
    }
    m_muted = muted;
    if (m_audioOutput) {
        m_audioOutput->setVolume(muted ? 0.0f : 1.0f);
    }
    emit mutedChanged();
}

void Player::open(const QUrl& url) {
    openPath(url.isLocalFile() ? url.toLocalFile() : url.toString());
}

void Player::openPath(const QString& path) {
    if (path.isEmpty()) {
        return;
    }
    // Los hilos anteriores se detienen antes de abrir el nuevo archivo.
    m_clock.stop();
    m_clock.setAudio(nullptr, nullptr);
    m_audioWorker.reset();
    m_worker.reset();
    const quint64 session = ++m_session;

    setPlaying(false);
    setError({});
    m_loaded = false;
    m_frame.reset();
    m_frameCount = 0;
    m_pendingFrame = 0; // al abrir se muestra el primer fotograma
    m_hwAccelName.clear();
    m_hasAudio = false;
    m_fileName = QFileInfo(path).fileName();
    m_filePath = path;
    m_markIn = -1;
    m_markOut = -1;
    emit marksChanged();
    setCurrentFrame(0);
    emit loadedChanged();
    emit decoderChanged();
    emit audioChanged();
    emit compositionChanged();
    setBusy(true);

    // Los avisos llegan desde los hilos de decodificación: se reenvían al hilo de la
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
    const std::string utf8Path = path.toStdString();
    m_worker =
        std::make_unique<DecodeWorker>(utf8Path, sharkcut::HwAccel::Auto, std::move(callbacks));

    if (m_audioOutput) {
        AudioWorker::Callbacks audioCallbacks;
        audioCallbacks.opened = [post](const AudioStreamInfo& info) {
            post([info](Player& p) { p.onAudioOpened(info); });
        };
        // Un clip sin audio no es un error: simplemente se reproduce en silencio.
        m_audioWorker = std::make_unique<AudioWorker>(
            [utf8Path] { return std::make_unique<sharkcut::AudioDecoder>(utf8Path); },
            *m_audioOutput, std::move(audioCallbacks));
    }
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
    emit marksChanged(); // la duración marcada depende del número de fotogramas
    emit decoderChanged();
    emit audioChanged();
    emit currentFrameChanged(); // el código de tiempo depende de los fps
}

void Player::onAudioOpened(const AudioStreamInfo& info) {
    m_hasAudio = true;
    m_audioInfo = info;
    emit audioChanged();
    // Desde ahora el reloj lo marca el audio (si ya se reproducía, sigue desde ahí).
    m_clock.setAudio(m_audioOutput.get(), m_audioWorker.get());
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
    setBusy(false);
    setPlaying(true);
    m_clock.start(sharkcut::frameToMicroseconds(m_currentFrame, m_rate));
}

void Player::pause() {
    m_clock.stop();
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
        m_clock.start(sharkcut::frameToMicroseconds(frame, m_rate));
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
    const qint64 target = sharkcut::microsecondsToFrame(m_clock.nowUs(), m_rate);

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
    emit compositionChanged();
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
