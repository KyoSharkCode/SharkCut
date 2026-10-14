#include "render/sequence_exporter.h"

#include "media/audio_decoder.h"
#include "media/mp4_writer.h"
#include "media/sequence_audio.h"
#include "media/video_decoder.h"
#include "render/compositor.h"

#include <QOffscreenSurface>
#include <rhi/qrhi.h>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>

namespace sharkcut {

namespace {

std::unique_ptr<QRhi> createOffscreenRhi(QOffscreenSurface* fallbackSurface) {
#ifdef Q_OS_WIN
    Q_UNUSED(fallbackSurface);
    QRhiD3D11InitParams params;
    return std::unique_ptr<QRhi>(QRhi::create(QRhi::D3D11, &params));
#else
    QRhiGles2InitParams params;
    params.fallbackSurface = fallbackSurface;
    return std::unique_ptr<QRhi>(QRhi::create(QRhi::OpenGLES2, &params));
#endif
}

// Lee los fotogramas de un tramo en orden, avanzando solo lo necesario.
class ClipReader {
public:
    ClipReader(const std::string& path, std::int64_t first) : m_decoder(path, HwAccel::Auto) {
        m_decoder.seekToFrame(first);
    }

    // Último fotograma con índice <= `index` (el que corresponde mostrar). Si el clip
    // se acaba antes, se repite su último fotograma.
    std::shared_ptr<const VideoFrame> at(std::int64_t index) {
        if (m_pending && m_pending->index <= index) {
            m_current = std::move(m_pending);
        }
        while (!m_finished && !m_pending && (!m_current || m_current->index < index)) {
            auto next = m_decoder.nextFrame();
            if (!next) {
                m_finished = true;
                break;
            }
            auto frame = std::make_shared<const VideoFrame>(std::move(*next));
            if (frame->index > index && m_current) {
                m_pending = std::move(frame); // hueco en el clip: se guarda para después
                break;
            }
            m_current = std::move(frame);
        }
        return m_current;
    }

private:
    VideoDecoder m_decoder;
    std::shared_ptr<const VideoFrame> m_current;
    std::shared_ptr<const VideoFrame> m_pending;
    bool m_finished = false;
};

} // namespace

SequenceExporter::SequenceExporter(ExportJob job, Callbacks callbacks)
    : m_job(std::move(job)), m_callbacks(std::move(callbacks)), m_thread([this] { run(); }) {}

SequenceExporter::~SequenceExporter() {
    cancel();
    m_thread.join();
}

void SequenceExporter::cancel() {
    m_cancel.store(true, std::memory_order_release);
}

void SequenceExporter::run() {
    auto finish = [this](bool ok, const std::string& message, const std::string& encoder) {
        if (m_callbacks.finished) {
            m_callbacks.finished(ok, message, encoder);
        }
    };
    const std::filesystem::path output(
        std::u8string(m_job.outputPath.begin(), m_job.outputPath.end()));
    auto removeOutput = [&output] {
        std::error_code ignored;
        std::filesystem::remove(output, ignored);
    };

    std::string encoder;
    try {
        std::unique_ptr<QRhi> rhi = createOffscreenRhi(m_job.fallbackSurface);
        if (!rhi) {
            finish(false, "No se pudo iniciar la GPU para exportar", {});
            return;
        }
        Compositor compositor;
        compositor.initialize(rhi.get());
        compositor.setCanvasSize(m_job.canvasSize);

        const SequenceLayout& layout = m_job.layout;
        ClipReader readerA(m_job.pathA, layout.a.in);
        ClipReader readerB(m_job.pathB, layout.b.in);

        std::unique_ptr<SequenceAudioSource> audio;
        try {
            audio =
                std::make_unique<SequenceAudioSource>(layout, m_job.rate, m_job.pathA, m_job.pathB);
        } catch (const std::exception&) {
            audio.reset(); // ningún clip tiene audio: MP4 solo con video
        }

        EncoderSettings settings;
        settings.width = m_job.canvasSize.width();
        settings.height = m_job.canvasSize.height();
        settings.rate = m_job.rate;
        settings.audio = audio != nullptr;
        settings.videoBitrate = m_job.videoBitrate;
        Mp4Writer writer(m_job.outputPath, settings);
        encoder = writer.videoEncoderName();

        const std::int64_t total = layout.length();
        const bool flipRows = rhi->isYUpInFramebuffer(); // OpenGL: filas de abajo arriba
        std::vector<float> samples;
        std::int64_t audioWritten = 0;

        for (std::int64_t frame = 0; frame < total; ++frame) {
            if (m_cancel.load(std::memory_order_acquire)) {
                writer.finish();
                removeOutput();
                finish(false, "Exportación cancelada", encoder);
                return;
            }

            // Mismo reparto que la vista previa (TransitionLab::composition).
            const auto at = layout.at(frame);
            CompositorInput input;
            std::shared_ptr<const VideoFrame> a = at.aFrame ? readerA.at(*at.aFrame) : nullptr;
            std::shared_ptr<const VideoFrame> b = at.bFrame ? readerB.at(*at.bFrame) : nullptr;
            if (at.transitionFrame && a && b) {
                input.a = a;
                input.b = b;
                input.transition = m_job.params;
                input.state = evaluateTransition(m_job.params, *at.transitionFrame);
            } else {
                input.a = a ? a : b;
            }

            QRhiCommandBuffer* cb = nullptr;
            if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) {
                throw std::runtime_error("La GPU no pudo componer el fotograma");
            }
            compositor.render(cb, input);
            QRhiReadbackResult readback;
            QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
            batch->readBackTexture({compositor.canvas()}, &readback);
            cb->resourceUpdate(batch);
            rhi->endOffscreenFrame(); // espera a la GPU: los píxeles ya están en `readback`

            const int stride = readback.pixelSize.width() * 4;
            const auto* pixels = reinterpret_cast<const std::uint8_t*>(readback.data.constData());
            if (flipRows) {
                writer.writeVideo(pixels + static_cast<std::ptrdiff_t>(stride) *
                                               (readback.pixelSize.height() - 1),
                                  -stride);
            } else {
                writer.writeVideo(pixels, stride);
            }

            // Audio hasta el final de este fotograma.
            if (audio) {
                const std::int64_t until =
                    rescale(frame + 1, m_job.rate.frameDuration(), kAudioSampleDuration);
                const auto count = static_cast<std::size_t>(until - audioWritten);
                samples.resize(count * kAudioChannels);
                const std::size_t got = audio->read(samples.data(), count);
                std::fill(samples.begin() + static_cast<std::ptrdiff_t>(got * kAudioChannels),
                          samples.end(), 0.0f);
                writer.writeAudio(samples.data(), count);
                audioWritten = until;
            }

            if (m_callbacks.progress && (frame % 10 == 0 || frame == total - 1)) {
                m_callbacks.progress(frame + 1, total);
            }
        }
        writer.finish();
        finish(true, {}, encoder);
    } catch (const std::exception& e) {
        removeOutput();
        finish(false, e.what(), encoder);
    }
}

} // namespace sharkcut
