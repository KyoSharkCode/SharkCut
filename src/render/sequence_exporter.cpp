#include "render/sequence_exporter.h"

#include "media/audio_decoder.h"
#include "media/mp4_writer.h"
#include "media/sequence_audio.h"
#include "media/timeline_audio.h"
#include "media/video_decoder.h"
#include "render/caption_renderer.h"
#include "render/compositor.h"
#include "render/text_frame.h"

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

// Lee fotogramas de un archivo, normalmente en orden: avanza solo lo necesario y
// vuelve a buscar si se le pide un fotograma anterior o muy lejano (otro tramo).
class MediaReader {
public:
    explicit MediaReader(const std::string& path) : m_decoder(path, HwAccel::Auto) {}

    // Último fotograma con índice <= `index` (el que corresponde mostrar). Si el
    // archivo se acaba antes, se repite su último fotograma.
    std::shared_ptr<const VideoFrame> at(std::int64_t index) {
        const bool needSeek =
            (!m_current && !m_finished) ||
            (m_current && (index < m_current->index || index > m_current->index + kMaxSkip));
        if (needSeek) {
            m_decoder.seekToFrame(index);
            m_current.reset();
            m_pending.reset();
            m_finished = false;
        }
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
                m_pending = std::move(frame); // hueco en el archivo: se guarda para después
                break;
            }
            m_current = std::move(frame);
        }
        return m_current;
    }

private:
    // Más allá de esto es más rápido buscar (ir al fotograma clave) que decodificar todo.
    static constexpr std::int64_t kMaxSkip = 120;

    VideoDecoder m_decoder;
    std::shared_ptr<const VideoFrame> m_current;
    std::shared_ptr<const VideoFrame> m_pending;
    bool m_finished = false;
};

// Qué componer en cada fotograma y con qué audio.
class FrameSource {
public:
    virtual ~FrameSource() = default;
    virtual std::int64_t length() const = 0;
    virtual FrameRate rate() const = 0;
    virtual CompositorInput at(std::int64_t frame) = 0;
    virtual std::unique_ptr<AudioSource> openAudio() = 0; // nullptr: sin audio
};

// Prueba de transición de la fase 1. Mismo reparto que TransitionLab::composition.
class TransitionFrames : public FrameSource {
public:
    TransitionFrames(const TransitionTest& test, FrameRate rate)
        : m_test(test), m_rate(rate), m_a(test.pathA), m_b(test.pathB) {}

    std::int64_t length() const override { return m_test.layout.length(); }
    FrameRate rate() const override { return m_rate; }

    CompositorInput at(std::int64_t frame) override {
        const auto at = m_test.layout.at(frame);
        CompositorInput input;
        std::shared_ptr<const VideoFrame> a = at.aFrame ? m_a.at(*at.aFrame) : nullptr;
        std::shared_ptr<const VideoFrame> b = at.bFrame ? m_b.at(*at.bFrame) : nullptr;
        CompositorLayer& layer = input.layers.emplace_back();
        if (at.transitionFrame && a && b) {
            layer.a = a;
            layer.b = b;
            layer.transition = m_test.params;
            layer.state = evaluateTransition(m_test.params, *at.transitionFrame);
        } else {
            layer.a = a ? a : b;
        }
        return input;
    }

    std::unique_ptr<AudioSource> openAudio() override {
        try {
            return std::make_unique<SequenceAudioSource>(m_test.layout, m_rate, m_test.pathA,
                                                         m_test.pathB);
        } catch (const std::exception&) {
            return nullptr; // ningún clip tiene audio
        }
    }

private:
    TransitionTest m_test;
    FrameRate m_rate;
    MediaReader m_a;
    MediaReader m_b;
};

// Línea de tiempo de un proyecto: las pistas de video visibles, una encima de otra,
// sobre negro, con sus transiciones. Si se exporta a otro tamaño (con la misma forma),
// las posiciones, que están en píxeles del lienzo, se escalan.
class TimelineFrames : public FrameSource {
public:
    TimelineFrames(Project project, QSize outputSize)
        : m_project(std::move(project)), m_outputSize(outputSize),
          m_scale(m_project.settings.width > 0
                      ? static_cast<double>(outputSize.width()) / m_project.settings.width
                      : 1.0) {}

    std::int64_t length() const override { return m_project.length(); }
    FrameRate rate() const override { return m_project.settings.frameRate; }

    CompositorInput at(std::int64_t frame) override {
        CompositorInput input;
        for (const Project::Layer& layer : m_project.videoLayersAt(frame)) {
            if (layer.clip->isText()) {
                // Texto libre, dibujado al tamaño de la exportación.
                const auto text = m_texts.get(*layer.clip->text, m_outputSize);
                if (text.frame) {
                    CompositorLayer& out = input.layers.emplace_back();
                    out.a = text.frame;
                    out.transform = scaled(transformAt(*layer.clip, frame));
                }
                continue;
            }
            CompositorLayer& out = input.layers.emplace_back();
            if (layer.clip->isEffect()) {
                // Transición-clip: transforma todo lo de debajo.
                out.adjust = true;
                out.transition = layer.clip->effect;
                out.state = evaluateTransition(*layer.clip->effect, layer.effectFrame);
                continue;
            }
            out.a = reader(layer.track, *layer.media, 0).at(layer.mediaFrame);
            out.transform = scaled(transformAt(*layer.clip, frame));
            if (layer.transition) {
                out.b = reader(layer.track, *layer.mediaB, 1).at(layer.mediaFrameB);
                out.transformB = scaled(transformAt(*layer.clipB, frame));
                out.transition = layer.transition->params;
                out.state = evaluateTransition(layer.transition->params, layer.transitionFrame);
            }
        }
        input.overlays = m_captions.overlaysAt(m_project, frame, m_outputSize);
        return input;
    }

    std::unique_ptr<AudioSource> openAudio() override {
        try {
            return std::make_unique<TimelineAudioSource>(m_project);
        } catch (const std::exception&) {
            return nullptr; // la línea de tiempo no tiene audio
        }
    }

private:
    ClipTransform scaled(const ClipTransform& t) const { return scaledTransform(t, m_scale); }

    // Un lector por pista, archivo y papel (0: el clip que se ve o el saliente de una
    // transición; 1: el entrante). Así dos pistas con el mismo video, o una transición
    // entre dos trozos del mismo VOD, no obligan a saltar de un sitio a otro del archivo
    // en cada fotograma. Hay un máximo abierto a la vez (se cierra el menos usado).
    MediaReader& reader(std::size_t track, const MediaItem& item, int role) {
        ++m_clock;
        for (Entry& entry : m_readers) {
            if (entry.track == track && entry.media == item.id && entry.role == role) {
                entry.lastUse = m_clock;
                return *entry.reader;
            }
        }
        if (m_readers.size() >= kMaxOpen) {
            const auto oldest = std::min_element(
                m_readers.begin(), m_readers.end(),
                [](const Entry& a, const Entry& b) { return a.lastUse < b.lastUse; });
            m_readers.erase(oldest);
        }
        m_readers.push_back(
            {track, item.id, role, std::make_unique<MediaReader>(item.path), m_clock});
        return *m_readers.back().reader;
    }

    struct Entry {
        std::size_t track = 0;
        ObjectId media = 0;
        int role = 0;
        std::unique_ptr<MediaReader> reader;
        std::uint64_t lastUse = 0;
    };
    static constexpr std::size_t kMaxOpen = 8;

    Project m_project;
    QSize m_outputSize;
    double m_scale = 1.0;
    CaptionImageCache m_captions;
    TextFrameCache m_texts;
    std::vector<Entry> m_readers;
    std::uint64_t m_clock = 0;
};

// `file`, o "nombre (2).ext", "nombre (3).ext"… si ya existe.
std::filesystem::path availablePath(const std::filesystem::path& file) {
    std::error_code ignored;
    if (!std::filesystem::exists(file, ignored)) {
        return file;
    }
    const std::u8string stem = file.stem().u8string();
    const std::u8string extension = file.extension().u8string();
    for (int n = 2;; ++n) {
        const std::string suffix = " (" + std::to_string(n) + ")";
        std::filesystem::path candidate =
            file.parent_path() / (stem + std::u8string(suffix.begin(), suffix.end()) + extension);
        if (!std::filesystem::exists(candidate, ignored)) {
            return candidate;
        }
    }
}

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
    std::filesystem::path output(std::u8string(m_job.outputPath.begin(), m_job.outputPath.end()));
    if (!m_job.overwrite) {
        output = availablePath(output);
    }
    const std::u8string written = output.u8string();
    const std::string writtenPath(written.begin(), written.end());
    auto finish = [this, &writtenPath](bool ok, const std::string& message,
                                       const std::string& encoder) {
        if (m_callbacks.finished) {
            m_callbacks.finished(ok, message, encoder, writtenPath);
        }
    };
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

        std::unique_ptr<FrameSource> source;
        if (const auto* test = std::get_if<TransitionTest>(&m_job.content)) {
            source = std::make_unique<TransitionFrames>(*test, m_job.rate);
        } else {
            source = std::make_unique<TimelineFrames>(std::get<Project>(m_job.content),
                                                      m_job.canvasSize);
        }
        const std::int64_t length = source->length();
        const std::int64_t end = m_job.rangeEnd < 0 ? length : std::min(m_job.rangeEnd, length);
        const ExportTiming timing{source->rate(), m_job.rate,
                                  std::clamp<std::int64_t>(m_job.rangeStart, 0, end), end};
        const std::int64_t total = timing.frameCount();
        if (total <= 0) {
            throw std::runtime_error("No hay nada que exportar");
        }
        // nullptr: solo video
        const std::unique_ptr<AudioSource> audio = m_job.audio ? source->openAudio() : nullptr;
        if (audio) {
            audio->seekToSample(timing.audioStart());
        }

        EncoderSettings settings;
        settings.width = m_job.canvasSize.width();
        settings.height = m_job.canvasSize.height();
        settings.rate = m_job.rate;
        settings.audio = audio != nullptr;
        settings.audioBitrate = m_job.audioBitrate;
        settings.codec = m_job.codec;
        settings.videoBitrate = m_job.videoBitrate;
        Mp4Writer writer(writtenPath, settings);
        encoder = writer.videoEncoderName();
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

            const CompositorInput input = source->at(timing.sequenceFrame(frame));

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
                const std::int64_t until = timing.audioSamplesBefore(frame + 1);
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
