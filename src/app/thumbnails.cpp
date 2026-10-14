#include "thumbnails.h"

#include "media/color.h"
#include "media/video_decoder.h"

#include <QCache>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QRunnable>
#include <QThreadPool>
#include <QUrl>

#include <algorithm>
#include <exception>

namespace {

constexpr int kThumbnailWidth = 320;

QMutex cacheMutex;
QCache<QString, QImage> cache(64); // unas 64 miniaturas

// NV12 → RGB a `width` píxeles de ancho (muestreo por el más cercano: basta para
// una miniatura), con la misma matriz de color que el compositor.
QImage toImage(const sharkcut::VideoFrame& frame, int width) {
    const int height = std::max(1, frame.height * width / std::max(1, frame.width));
    const auto m = sharkcut::yuvToRgbMatrix(frame.matrix, frame.fullRange);
    QImage image(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y) {
        const int sy = y * frame.height / height;
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const int sx = x * frame.width / width;
            const float Y = frame.y[static_cast<std::size_t>(sy * frame.width + sx)] / 255.0f;
            const std::size_t uv =
                static_cast<std::size_t>((sy / 2) * frame.uvWidth() * 2 + (sx / 2) * 2);
            const float U = frame.uv[uv] / 255.0f;
            const float V = frame.uv[uv + 1] / 255.0f;
            auto channel = [&](int row) {
                const float v = m[static_cast<std::size_t>(row * 4)] * Y +
                                m[static_cast<std::size_t>(row * 4 + 1)] * U +
                                m[static_cast<std::size_t>(row * 4 + 2)] * V +
                                m[static_cast<std::size_t>(row * 4 + 3)];
                return std::clamp(static_cast<int>(v * 255.0f + 0.5f), 0, 255);
            };
            line[x] = qRgb(channel(0), channel(1), channel(2));
        }
    }
    return image;
}

class ThumbnailResponse : public QQuickImageResponse, public QRunnable {
public:
    explicit ThumbnailResponse(QString path) : m_path(std::move(path)) { setAutoDelete(false); }

    QQuickTextureFactory* textureFactory() const override {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    void run() override {
        {
            QMutexLocker lock(&cacheMutex);
            if (const QImage* cached = cache.object(m_path)) {
                m_image = *cached;
            }
        }
        if (m_image.isNull()) {
            try {
                sharkcut::VideoDecoder decoder(m_path.toStdString(), sharkcut::HwAccel::None);
                decoder.seekToFrame(decoder.info().frameCount / 10);
                if (auto frame = decoder.nextFrame()) {
                    m_image = toImage(*frame, kThumbnailWidth);
                    QMutexLocker lock(&cacheMutex);
                    cache.insert(m_path, new QImage(m_image));
                }
            } catch (const std::exception&) {
                // sin video o archivo que falta: la interfaz muestra un icono
            }
        }
        emit finished();
    }

private:
    QString m_path;
    QImage m_image;
};

} // namespace

QQuickImageResponse* ThumbnailProvider::requestImageResponse(const QString& id, const QSize&) {
    auto* response = new ThumbnailResponse(QUrl::fromPercentEncoding(id.toUtf8()));
    QThreadPool::globalInstance()->start(response);
    return response;
}
