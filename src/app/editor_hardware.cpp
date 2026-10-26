// Apartado de información: la GPU con la que dibuja el visor, la decodificación (por la
// GPU o por software) y los codificadores que usaría la exportación.

#include "editor.h"

#include "media/mp4_writer.h"
#include "media/video_decoder.h"

#include <QCoreApplication>
#include <QPointer>
#include <QThreadPool>

namespace {

// Nombre legible de un codificador de FFmpeg.
QString encoderLabel(const QString& name) {
    if (name.isEmpty()) {
        return QStringLiteral("Ninguno disponible");
    }
    if (name.endsWith(QLatin1String("_nvenc"))) {
        return QStringLiteral("NVENC, GPU NVIDIA (%1)").arg(name);
    }
    if (name.endsWith(QLatin1String("_amf"))) {
        return QStringLiteral("AMF, GPU AMD (%1)").arg(name);
    }
    if (name.endsWith(QLatin1String("_qsv"))) {
        return QStringLiteral("Quick Sync, GPU Intel (%1)").arg(name);
    }
    if (sharkcut::isSoftwareEncoder(name.toStdString())) {
        return QStringLiteral("Por software (%1)").arg(name);
    }
    return name;
}

} // namespace

void Editor::setRenderDevice(const QString& device, const QString& api) {
    if (m_renderDevice == device && m_renderApi == api) {
        return;
    }
    m_renderDevice = device;
    m_renderApi = api;
    emit hardwareInfoChanged();
}

QVariantMap Editor::hardwareInfo() const {
    QVariantMap info;
    info.insert(QStringLiteral("gpu"), m_renderDevice);
    info.insert(QStringLiteral("api"), m_renderApi);
    info.insert(QStringLiteral("probing"), m_probingHardware);
    info.insert(QStringLiteral("probed"), m_hardwareProbed);

    // Decodificación: lo que dicen los hilos abiertos ahora; si no hay ninguno, lo que
    // se podría usar en este equipo.
    QString inUse;
    bool anyWorker = false;
    for (const TrackVideo& track : m_video) {
        for (const VideoSlot& slot : track.decoders) {
            if (!slot.worker) {
                continue;
            }
            anyWorker = true;
            const std::string name = slot.worker->hwAccelName();
            if (!name.empty()) {
                inUse = QString::fromStdString(name);
            }
        }
    }
    QString decoder;
    bool decoderGpu = false;
    if (!inUse.isEmpty()) {
        decoder = QStringLiteral("Por la GPU (%1), en uso").arg(inUse);
        decoderGpu = true;
    } else if (m_hardwareProbed && !m_hwDecodeDevice.isEmpty()) {
        decoder = anyWorker
                      ? QStringLiteral("Por software ahora (la GPU, %1, no admite este video)")
                            .arg(m_hwDecodeDevice)
                      : QStringLiteral("Por la GPU (%1)").arg(m_hwDecodeDevice);
        decoderGpu = !anyWorker;
    } else if (m_hardwareProbed) {
        decoder = QStringLiteral("Por software (respaldo: la GPU no está disponible)");
    }
    info.insert(QStringLiteral("decoder"), decoder);
    info.insert(QStringLiteral("decoderGpu"), decoderGpu);

    const auto encoder = [&](const QString& key, const QString& name) {
        info.insert(key, m_hardwareProbed ? encoderLabel(name) : QString());
        info.insert(key + QLatin1String("Software"),
                    m_hardwareProbed &&
                        (name.isEmpty() || sharkcut::isSoftwareEncoder(name.toStdString())));
    };
    encoder(QStringLiteral("h264"), m_encoderH264);
    encoder(QStringLiteral("h265"), m_encoderH265);
    return info;
}

void Editor::refreshHardwareInfo() {
    if (m_probingHardware) {
        return;
    }
    m_probingHardware = true;
    emit hardwareInfoChanged();
    QPointer<Editor> self(this);
    QThreadPool::globalInstance()->start([self] {
        const QString device = QString::fromStdString(sharkcut::probeHwDecodeDevice());
        const QString h264 =
            QString::fromStdString(sharkcut::probeVideoEncoder(sharkcut::VideoCodec::H264));
        const QString h265 =
            QString::fromStdString(sharkcut::probeVideoEncoder(sharkcut::VideoCodec::H265));
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, device, h264, h265] {
                if (!self) {
                    return;
                }
                self->m_hwDecodeDevice = device;
                self->m_encoderH264 = h264;
                self->m_encoderH265 = h265;
                self->m_hardwareProbed = true;
                self->m_probingHardware = false;
                emit self->hardwareInfoChanged();
            },
            Qt::QueuedConnection);
    });
}
