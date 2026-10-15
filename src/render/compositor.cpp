#include "render/compositor.h"

#include "media/color.h"

#include <QFile>
#include <QMatrix4x4>
#include <rhi/qrhi.h>

#include <algorithm>

namespace sharkcut {

namespace {

QShader loadShader(const char* name) {
    QFile file(QStringLiteral(":/sharkcut/shaders/%1.qsb").arg(QLatin1String(name)));
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("No se pudo cargar el shader %s", name);
        return {};
    }
    return QShader::fromSerialized(file.readAll());
}

// Posición (x, y) y coordenada de textura (u, v) de las 4 esquinas.
// v = 0 es arriba (primera fila de las texturas subidas desde la CPU).
constexpr float kQuad[] = {
    -1.0f, -1.0f, 0.0f, 1.0f, //
    1.0f,  -1.0f, 1.0f, 1.0f, //
    -1.0f, 1.0f,  0.0f, 0.0f, //
    1.0f,  1.0f,  1.0f, 0.0f, //
};

// Bloque uniforme común de las pasadas de transición y presentación.
struct QuadUniforms {
    float mvp[16];
    float p0[4];
    float p1[4];
    float p2[4];
};
static_assert(sizeof(QuadUniforms) == 112);

// Bloque uniforme de layer.vert / layer.frag.
struct LayerUniforms {
    float transform[16];
    float yuvToRgb[16];
    float posRect[4];
    float uvRect[4];
    float params[4];
};
static_assert(sizeof(LayerUniforms) == 176);

// Escala para que un contenido de `content` quepa en `frame` sin deformarse.
QSizeF fitScale(QSize content, QSize frame) {
    const double contentAspect =
        static_cast<double>(content.width()) / std::max(content.height(), 1);
    const double frameAspect = static_cast<double>(frame.width()) / std::max(frame.height(), 1);
    if (contentAspect > frameAspect) {
        return {1.0, frameAspect / contentAspect};
    }
    return {contentAspect / frameAspect, 1.0};
}

QRhiVertexInputLayout quadLayout() {
    QRhiVertexInputLayout layout;
    layout.setBindings({{4 * sizeof(float)}});
    layout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},
        {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)},
    });
    return layout;
}

// Alfa premultiplicado: lo nuevo encima de lo que ya hay.
QRhiGraphicsPipeline::TargetBlend premultipliedBlend() {
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::One;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    return blend;
}

} // namespace

// Un fotograma NV12 en la GPU, listo para dibujarse con su transformación. `a` es su
// transparencia (R8); en los videos opacos, un único píxel blanco.
struct Compositor::Source {
    std::unique_ptr<QRhiTexture> y;
    std::unique_ptr<QRhiTexture> uv;
    std::unique_ptr<QRhiTexture> a;
    bool opaqueAlpha = false; // `a` es el píxel blanco y ya se subió
    std::shared_ptr<const VideoFrame> uploaded;
    std::unique_ptr<QRhiBuffer> uniforms;
    std::unique_ptr<QRhiShaderResourceBindings> bindings;
};

// Las dos texturas (clip saliente y entrante) de una capa con transición.
struct Compositor::TransitionTargets {
    std::unique_ptr<QRhiTexture> rgb[2];
    std::unique_ptr<QRhiTextureRenderTarget> target[2];
    std::unique_ptr<QRhiRenderPassDescriptor> pass[2];
    std::unique_ptr<QRhiBuffer> uniforms;
    std::unique_ptr<QRhiShaderResourceBindings> bindings;
};

Compositor::Compositor() = default;

Compositor::~Compositor() {
    releaseAll();
}

void Compositor::releaseAll() {
    m_presentPipeline.reset();
    m_presentBindings.reset();
    m_presentUniforms.reset();
    m_presentPass = nullptr;
    m_compositePipeline.reset();
    m_blendPipeline.reset();
    m_layerPipeline.reset();
    m_transitions.clear();
    m_sources.clear();
    m_canvasPass.reset();
    m_canvasTarget.reset();
    m_canvas.reset();
    m_sampler.reset();
    m_quad.reset();
    m_quadUploaded = false;
}

void Compositor::initialize(QRhi* rhi) {
    if (m_rhi == rhi && m_quad) {
        return;
    }
    releaseAll();
    m_rhi = rhi;
    if (!m_rhi) {
        return;
    }
    m_quad.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(kQuad)));
    m_quad->create();
    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_sampler->create();
    m_presentUniforms.reset(
        m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(QuadUniforms)));
    m_presentUniforms->create();
    createCanvas();
    ensurePipelines();
}

void Compositor::setCanvasSize(QSize size) {
    size = size.expandedTo(QSize(16, 16));
    if (size == m_canvasSize) {
        return;
    }
    m_canvasSize = size;
    if (m_rhi) {
        createCanvas();
        for (auto& targets : m_transitions) {
            createTransitionTargets(*targets);
        }
        for (auto& source : m_sources) {
            source->uploaded.reset(); // los uniformes dependen del lienzo
        }
    }
}

QRhiTexture* Compositor::canvas() const {
    return m_canvas.get();
}

void Compositor::createCanvas() {
    m_canvasTarget.reset();
    m_canvas.reset(
        m_rhi->newTexture(QRhiTexture::RGBA8, m_canvasSize, 1, QRhiTexture::RenderTarget));
    m_canvas->create();
    m_canvasTarget.reset(m_rhi->newTextureRenderTarget({m_canvas.get()}));
    if (!m_canvasPass) {
        m_canvasPass.reset(m_canvasTarget->newCompatibleRenderPassDescriptor());
    }
    m_canvasTarget->setRenderPassDescriptor(m_canvasPass.get());
    m_canvasTarget->create();

    const auto stage =
        QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
    if (!m_presentBindings) {
        m_presentBindings.reset(m_rhi->newShaderResourceBindings());
    }
    m_presentBindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, stage, m_presentUniforms.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  m_canvas.get(), m_sampler.get()),
    });
    m_presentBindings->create();
}

void Compositor::createTransitionTargets(TransitionTargets& targets) {
    for (int i = 0; i < 2; ++i) {
        targets.target[i].reset();
        targets.rgb[i].reset(
            m_rhi->newTexture(QRhiTexture::RGBA8, m_canvasSize, 1, QRhiTexture::RenderTarget));
        targets.rgb[i]->create();
        targets.target[i].reset(m_rhi->newTextureRenderTarget({targets.rgb[i].get()}));
        if (!targets.pass[i]) {
            targets.pass[i].reset(targets.target[i]->newCompatibleRenderPassDescriptor());
        }
        targets.target[i]->setRenderPassDescriptor(targets.pass[i].get());
        targets.target[i]->create();
    }
    if (!targets.uniforms) {
        targets.uniforms.reset(
            m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(QuadUniforms)));
        targets.uniforms->create();
    }
    if (!targets.bindings) {
        targets.bindings.reset(m_rhi->newShaderResourceBindings());
    }
    const auto stage =
        QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
    targets.bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, stage, targets.uniforms.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  targets.rgb[0].get(), m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage,
                                                  targets.rgb[1].get(), m_sampler.get()),
    });
    targets.bindings->create();
}

Compositor::Source& Compositor::source(std::size_t index) {
    while (m_sources.size() <= index) {
        auto created = std::make_unique<Source>();
        created->uniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                                                 sizeof(LayerUniforms)));
        created->uniforms->create();
        // Texturas NV12 (y de alfa) provisionales hasta el primer fotograma.
        created->y.reset(m_rhi->newTexture(QRhiTexture::R8, QSize(16, 16)));
        created->y->create();
        created->uv.reset(m_rhi->newTexture(QRhiTexture::RG8, QSize(8, 8)));
        created->uv->create();
        created->a.reset(m_rhi->newTexture(QRhiTexture::R8, QSize(1, 1)));
        created->a->create();
        created->bindings.reset(m_rhi->newShaderResourceBindings());
        bindSource(*created);
        m_sources.push_back(std::move(created));
    }
    return *m_sources[index];
}

Compositor::TransitionTargets& Compositor::transitionTargets(std::size_t index) {
    while (m_transitions.size() <= index) {
        auto created = std::make_unique<TransitionTargets>();
        createTransitionTargets(*created);
        m_transitions.push_back(std::move(created));
    }
    return *m_transitions[index];
}

void Compositor::ensurePipelines() {
    // Todas las pasadas dibujan en texturas RGBA8 sin profundidad: el descriptor del
    // lienzo es compatible con el de las texturas de transición.
    auto layerPipeline = [this](bool blend) {
        // La disposición de los recursos es la misma en todas las fuentes.
        Source& any = source(0);
        std::unique_ptr<QRhiGraphicsPipeline> pipeline(m_rhi->newGraphicsPipeline());
        pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, loadShader("layer.vert")},
            {QRhiShaderStage::Fragment, loadShader("layer.frag")},
        });
        pipeline->setVertexInputLayout(quadLayout());
        pipeline->setShaderResourceBindings(any.bindings.get());
        pipeline->setRenderPassDescriptor(m_canvasPass.get());
        if (blend) {
            pipeline->setTargetBlends({premultipliedBlend()});
        }
        pipeline->create();
        return pipeline;
    };
    if (!m_layerPipeline) {
        m_layerPipeline = layerPipeline(false);
    }
    if (!m_blendPipeline) {
        m_blendPipeline = layerPipeline(true);
    }
    if (!m_compositePipeline) {
        TransitionTargets& any = transitionTargets(0);
        m_compositePipeline.reset(m_rhi->newGraphicsPipeline());
        m_compositePipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        m_compositePipeline->setShaderStages({
            {QRhiShaderStage::Vertex, loadShader("quad.vert")},
            {QRhiShaderStage::Fragment, loadShader("transition.frag")},
        });
        m_compositePipeline->setVertexInputLayout(quadLayout());
        m_compositePipeline->setShaderResourceBindings(any.bindings.get());
        m_compositePipeline->setRenderPassDescriptor(m_canvasPass.get());
        m_compositePipeline->setTargetBlends({premultipliedBlend()});
        m_compositePipeline->create();
    }
}

void Compositor::bindSource(Source& s) {
    const auto stage =
        QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
    const auto fragment = QRhiShaderResourceBinding::FragmentStage;
    s.bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, stage, s.uniforms.get()),
        QRhiShaderResourceBinding::sampledTexture(1, fragment, s.y.get(), m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(2, fragment, s.uv.get(), m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(3, fragment, s.a.get(), m_sampler.get()),
    });
    s.bindings->create();
}

void Compositor::prepareSource(Source& s, const VideoFrame& frame, const ClipTransform& t,
                               double opacity, QRhiResourceUpdateBatch* updates) {
    const QSize sizeY(frame.width, frame.height);
    const QSize sizeA = frame.hasAlpha() ? sizeY : QSize(1, 1);
    if (s.y->pixelSize() != sizeY || s.a->pixelSize() != sizeA) {
        if (s.y->pixelSize() != sizeY) {
            s.y.reset(m_rhi->newTexture(QRhiTexture::R8, sizeY));
            s.y->create();
            s.uv.reset(
                m_rhi->newTexture(QRhiTexture::RG8, QSize(frame.uvWidth(), frame.uvHeight())));
            s.uv->create();
        }
        if (s.a->pixelSize() != sizeA) {
            s.a.reset(m_rhi->newTexture(QRhiTexture::R8, sizeA));
            s.a->create();
            s.opaqueAlpha = false;
        }
        bindSource(s); // nuevas texturas: se actualizan las asociaciones (mismo diseño)
        s.uploaded.reset();
    }
    if (!frame.hasAlpha() && !s.opaqueAlpha) {
        // Video opaco: alfa 1 en todo el clip.
        static const char kWhite[4] = {'\xff', '\xff', '\xff', '\xff'};
        QRhiTextureSubresourceUploadDescription white(QByteArray::fromRawData(kWhite, 1));
        white.setDataStride(1);
        updates->uploadTexture(s.a.get(), QRhiTextureUploadEntry(0, 0, white));
        s.opaqueAlpha = true;
    }

    if (s.uploaded.get() != &frame) {
        // Los datos se envían antes de que termine este fotograma: sin copia previa.
        QRhiTextureSubresourceUploadDescription y(QByteArray::fromRawData(
            reinterpret_cast<const char*>(frame.y.data()), static_cast<qsizetype>(frame.y.size())));
        y.setDataStride(static_cast<quint32>(frame.width));
        updates->uploadTexture(s.y.get(), QRhiTextureUploadEntry(0, 0, y));
        QRhiTextureSubresourceUploadDescription uv(
            QByteArray::fromRawData(reinterpret_cast<const char*>(frame.uv.data()),
                                    static_cast<qsizetype>(frame.uv.size())));
        uv.setDataStride(static_cast<quint32>(frame.uvWidth() * 2));
        updates->uploadTexture(s.uv.get(), QRhiTextureUploadEntry(0, 0, uv));
        if (frame.hasAlpha()) {
            QRhiTextureSubresourceUploadDescription a(
                QByteArray::fromRawData(reinterpret_cast<const char*>(frame.a.data()),
                                        static_cast<qsizetype>(frame.a.size())));
            a.setDataStride(static_cast<quint32>(frame.width));
            updates->uploadTexture(s.a.get(), QRhiTextureUploadEntry(0, 0, a));
        }
    }

    // Ajuste al lienzo sin deformar, escala y posición (píxeles del lienzo, +y abajo).
    LayerUniforms u{};
    const QSizeF fit = fitScale(sizeY, m_canvasSize);
    QMatrix4x4 transform = m_rhi->clipSpaceCorrMatrix();
    transform.translate(static_cast<float>(2.0 * t.x / m_canvasSize.width()),
                        static_cast<float>(-2.0 * t.y / m_canvasSize.height()));
    transform.scale(static_cast<float>(fit.width() * t.scale),
                    static_cast<float>(fit.height() * t.scale));
    std::copy(transform.constData(), transform.constData() + 16, u.transform);
    const auto colors = yuvToRgbMatrix(frame.matrix, frame.fullRange);
    const QMatrix4x4 yuvToRgb(colors.data()); // por filas → se guarda por columnas, como GLSL
    std::copy(yuvToRgb.constData(), yuvToRgb.constData() + 16, u.yuvToRgb);
    // Recorte: la geometría (coordenadas del video, y hacia arriba) y la textura (v hacia abajo).
    const auto f = [](double v) { return static_cast<float>(v); };
    u.posRect[0] = f(-1.0 + 2.0 * t.cropLeft);
    u.posRect[1] = f(-1.0 + 2.0 * t.cropBottom);
    u.posRect[2] = f(1.0 - 2.0 * t.cropRight);
    u.posRect[3] = f(1.0 - 2.0 * t.cropTop);
    u.uvRect[0] = f(t.cropLeft);
    u.uvRect[1] = f(t.cropTop);
    u.uvRect[2] = f(1.0 - t.cropRight);
    u.uvRect[3] = f(1.0 - t.cropBottom);
    u.params[0] = f(std::clamp(opacity, 0.0, 1.0));
    updates->updateDynamicBuffer(s.uniforms.get(), 0, sizeof(u), &u);
}

void Compositor::drawSource(QRhiCommandBuffer* cb, Source& s, QRhiGraphicsPipeline* pipeline) {
    cb->setGraphicsPipeline(pipeline);
    cb->setViewport({0, 0, static_cast<float>(m_canvasSize.width()),
                     static_cast<float>(m_canvasSize.height())});
    cb->setShaderResources(s.bindings.get());
    const QRhiCommandBuffer::VertexInput input(m_quad.get(), 0);
    cb->setVertexInput(0, 1, &input);
    cb->draw(4);
}

void Compositor::render(QRhiCommandBuffer* cb, const CompositorInput& input) {
    if (!m_rhi) {
        return;
    }
    if (!m_quadUploaded) {
        QRhiResourceUpdateBatch* quad = m_rhi->nextResourceUpdateBatch();
        quad->uploadStaticBuffer(m_quad.get(), kQuad);
        cb->resourceUpdate(quad); // antes de cualquier pasada
        m_quadUploaded = true;
    }
    QRhiResourceUpdateBatch* canvasUpdates = m_rhi->nextResourceUpdateBatch();

    // Qué se dibuja en el lienzo, en orden: una fuente directa o una transición.
    struct Draw {
        Source* source = nullptr;
        TransitionTargets* transition = nullptr;
    };
    std::vector<Draw> draws;
    std::size_t sourceCount = 0;
    std::size_t transitionCount = 0;

    for (const CompositorLayer& layer : input.layers) {
        if (layer.transition && layer.a && layer.b) {
            // Los dos clips, cada uno en su textura (sobre negro), antes de la pasada
            // del lienzo; la transición los mezcla ahí.
            // Sobre transparente: en una pista superior, lo que no cubren los clips deja
            // ver las pistas de debajo.
            TransitionTargets& targets = transitionTargets(transitionCount++);
            const std::shared_ptr<const VideoFrame> frames[2] = {layer.a, layer.b};
            const ClipTransform* transforms[2] = {&layer.transform, &layer.transformB};
            for (int i = 0; i < 2; ++i) {
                Source& s = source(sourceCount++);
                QRhiResourceUpdateBatch* updates = m_rhi->nextResourceUpdateBatch();
                prepareSource(s, *frames[i], *transforms[i], transforms[i]->opacity, updates);
                s.uploaded = frames[i];
                cb->beginPass(targets.target[i].get(), Qt::transparent, {1.0f, 0}, updates);
                drawSource(cb, s, m_layerPipeline.get());
                cb->endPass();
            }

            QuadUniforms u{};
            const QMatrix4x4 mvp = m_rhi->clipSpaceCorrMatrix();
            std::copy(mvp.constData(), mvp.constData() + 16, u.mvp);
            u.p0[0] = static_cast<float>(static_cast<int>(layer.transition->type) + 1);
            u.p0[1] = static_cast<float>(layer.state.eased);
            u.p0[2] = static_cast<float>(layer.state.velocity);
            u.p0[3] = static_cast<float>(layer.transition->intensity);
            u.p1[0] = static_cast<float>(layer.transition->blur);
            u.p1[1] = static_cast<float>(m_canvasSize.width()) /
                      static_cast<float>(m_canvasSize.height());
            u.p1[2] = m_rhi->isYUpInFramebuffer() ? 1.0f : 0.0f;
            u.p1[3] = static_cast<float>(layer.state.progress);
            canvasUpdates->updateDynamicBuffer(targets.uniforms.get(), 0, sizeof(u), &u);
            draws.push_back({nullptr, &targets});
            continue;
        }

        const std::shared_ptr<const VideoFrame>& frame = layer.a ? layer.a : layer.b;
        if (!frame || layer.transform.opacity <= 0.0) {
            continue;
        }
        Source& s = source(sourceCount++);
        prepareSource(s, *frame, layer.transform, layer.transform.opacity, canvasUpdates);
        s.uploaded = frame;
        draws.push_back({&s, nullptr});
    }

    cb->beginPass(m_canvasTarget.get(), Qt::black, {1.0f, 0}, canvasUpdates);
    for (const Draw& draw : draws) {
        if (draw.source) {
            drawSource(cb, *draw.source, m_blendPipeline.get());
            continue;
        }
        cb->setGraphicsPipeline(m_compositePipeline.get());
        cb->setViewport({0, 0, static_cast<float>(m_canvasSize.width()),
                         static_cast<float>(m_canvasSize.height())});
        cb->setShaderResources(draw.transition->bindings.get());
        const QRhiCommandBuffer::VertexInput vertices(m_quad.get(), 0);
        cb->setVertexInput(0, 1, &vertices);
        cb->draw(4);
    }
    cb->endPass();
}

void Compositor::present(QRhiCommandBuffer* cb, QRhiRenderTarget* target,
                         const QColor& background) {
    if (!m_rhi || !target) {
        return;
    }
    if (m_presentPipeline && m_presentPass != target->renderPassDescriptor()) {
        m_presentPipeline.reset(); // destino nuevo: pipeline compatible con él
    }
    if (!m_presentPipeline) {
        m_presentPass = target->renderPassDescriptor();
        m_presentPipeline.reset(m_rhi->newGraphicsPipeline());
        m_presentPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        m_presentPipeline->setSampleCount(target->sampleCount());
        m_presentPipeline->setShaderStages({
            {QRhiShaderStage::Vertex, loadShader("quad.vert")},
            {QRhiShaderStage::Fragment, loadShader("present.frag")},
        });
        m_presentPipeline->setVertexInputLayout(quadLayout());
        m_presentPipeline->setShaderResourceBindings(m_presentBindings.get());
        m_presentPipeline->setRenderPassDescriptor(m_presentPass);
        m_presentPipeline->create();
    }

    const QSize outputSize = target->pixelSize();
    QuadUniforms u{};
    QMatrix4x4 mvp = m_rhi->clipSpaceCorrMatrix();
    const QSizeF scale = fitScale(m_canvasSize, outputSize);
    mvp.scale(static_cast<float>(scale.width()), static_cast<float>(scale.height()));
    std::copy(mvp.constData(), mvp.constData() + 16, u.mvp);
    u.p1[2] = m_rhi->isYUpInFramebuffer() ? 1.0f : 0.0f;
    QRhiResourceUpdateBatch* updates = m_rhi->nextResourceUpdateBatch();
    updates->updateDynamicBuffer(m_presentUniforms.get(), 0, sizeof(u), &u);

    cb->beginPass(target, background, {1.0f, 0}, updates);
    if (outputSize.width() > 0 && outputSize.height() > 0) {
        cb->setGraphicsPipeline(m_presentPipeline.get());
        cb->setViewport({0, 0, static_cast<float>(outputSize.width()),
                         static_cast<float>(outputSize.height())});
        cb->setShaderResources(m_presentBindings.get());
        const QRhiCommandBuffer::VertexInput vertices(m_quad.get(), 0);
        cb->setVertexInput(0, 1, &vertices);
        cb->draw(4);
    }
    cb->endPass();
}

} // namespace sharkcut
