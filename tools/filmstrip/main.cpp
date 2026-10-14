// sharkcut_filmstrip: hoja de contactos de las transiciones.
//
// Compone, con el mismo motor que el visor (sharkcut::Compositor) y fuera de pantalla,
// varias etapas de cada transición entre un fotograma del clip A y otro del clip B, y
// las guarda en una imagen PNG. Sirve para revisar la calidad sin abrir la interfaz.
//
// Uso: sharkcut_filmstrip <clipA> <clipB> <salida.png> [fotogramaA] [fotogramaB]

#include "core/transition.h"
#include "media/video_decoder.h"
#include "render/compositor.h"

#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QPainter>
#include <rhi/qrhi.h>

#include <cstdio>
#include <memory>
#include <vector>

namespace {

std::shared_ptr<const sharkcut::VideoFrame> frameAt(const char* path, std::int64_t index) {
    sharkcut::VideoDecoder decoder(path, sharkcut::HwAccel::None);
    decoder.seekToFrame(index);
    auto frame = decoder.nextFrame();
    if (!frame) {
        return nullptr;
    }
    return std::make_shared<const sharkcut::VideoFrame>(std::move(*frame));
}

std::unique_ptr<QRhi> createRhi() {
#ifdef Q_OS_WIN
    QRhiD3D11InitParams params;
    return std::unique_ptr<QRhi>(QRhi::create(QRhi::D3D11, &params));
#else
    static QOffscreenSurface* surface = QRhiGles2InitParams::newFallbackSurface();
    QRhiGles2InitParams params;
    params.fallbackSurface = surface;
    return std::unique_ptr<QRhi>(QRhi::create(QRhi::OpenGLES2, &params));
#endif
}

QImage renderOnce(QRhi* rhi, sharkcut::Compositor& compositor,
                  const sharkcut::CompositorInput& input) {
    QRhiCommandBuffer* cb = nullptr;
    if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) {
        return {};
    }
    compositor.render(cb, input);
    QRhiReadbackResult result;
    QRhiResourceUpdateBatch* readback = rhi->nextResourceUpdateBatch();
    readback->readBackTexture({compositor.canvas()}, &result);
    cb->resourceUpdate(readback);
    rhi->endOffscreenFrame();

    QImage image(reinterpret_cast<const uchar*>(result.data.constData()), result.pixelSize.width(),
                 result.pixelSize.height(), QImage::Format_RGBA8888);
    image = image.copy();
    if (rhi->isYUpInFramebuffer()) {
        image.flip(Qt::Vertical); // OpenGL guarda las filas de abajo arriba
    }
    return image;
}

} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    if (argc < 4) {
        std::fprintf(stderr, "Uso: %s <clipA> <clipB> <salida.png> [fotogramaA] [fotogramaB]\n",
                     argv[0]);
        return 2;
    }
    const std::int64_t indexA = argc > 4 ? std::atoll(argv[4]) : 120;
    const std::int64_t indexB = argc > 5 ? std::atoll(argv[5]) : 120;

    auto frameA = frameAt(argv[1], indexA);
    auto frameB = frameAt(argv[2], indexB);
    if (!frameA || !frameB) {
        std::fprintf(stderr, "No se pudieron leer los fotogramas\n");
        return 1;
    }

    std::unique_ptr<QRhi> rhi = createRhi();
    if (!rhi) {
        std::fprintf(stderr, "No se pudo crear el dispositivo gráfico\n");
        return 1;
    }
    sharkcut::Compositor compositor;
    compositor.initialize(rhi.get());
    compositor.setCanvasSize(QSize(frameA->width, frameA->height));

    // 7 transiciones × 7 etapas, miniaturas de 320 × 180.
    const std::vector<double> stages = {0.0, 0.2, 0.4, 0.5, 0.6, 0.8, 1.0};
    const QSize thumb(320, 180);
    const int labelWidth = 150;
    QImage sheet(labelWidth + thumb.width() * static_cast<int>(stages.size()),
                 thumb.height() * static_cast<int>(sharkcut::kTransitions.size()),
                 QImage::Format_RGB32);
    sheet.fill(QColor("#14161C"));
    QPainter painter(&sheet);
    painter.setPen(QColor("#E6E8EE"));

    int row = 0;
    for (const auto& info : sharkcut::kTransitions) {
        painter.drawText(
            QRect(8, row * thumb.height(), labelWidth - 16, thumb.height()),
            Qt::AlignVCenter | Qt::TextWordWrap,
            QString::fromUtf8(info.name.data(), static_cast<qsizetype>(info.name.size())));
        const sharkcut::TransitionParams params = info.defaults;
        for (std::size_t col = 0; col < stages.size(); ++col) {
            const auto frame =
                static_cast<std::int64_t>(stages[col] * (params.durationFrames - 1) + 0.5);
            sharkcut::CompositorInput input;
            input.a = frameA;
            input.b = frameB;
            input.transition = params;
            input.state = sharkcut::evaluateTransition(params, frame);
            const QImage image = renderOnce(rhi.get(), compositor, input);
            painter.drawImage(QRect(labelWidth + static_cast<int>(col) * thumb.width(),
                                    row * thumb.height(), thumb.width() - 4, thumb.height() - 4),
                              image);
        }
        ++row;
    }
    painter.end();
    if (!sheet.save(QString::fromLocal8Bit(argv[3]))) {
        std::fprintf(stderr, "No se pudo guardar %s\n", argv[3]);
        return 1;
    }
    std::printf("Guardado: %s\n", argv[3]);
    return 0;
}
