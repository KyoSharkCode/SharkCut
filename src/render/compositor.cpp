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

// Bloque uniforme común de las pasadas de composición y presentación.
struct QuadUniforms {
    float mvp[16];
    float p0[4];
    float p1[4];
    float p2[4];
};
static_assert(sizeof(QuadUniforms) == 112);

constexpr quint32 kLayerUniformSize = 2 * 64; // transform + yuvToRgb

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

} // namespace

// Una capa: el fotograma NV12 de un clip en la GPU y su versión RGB del tamaño del lienzo.
struct Compositor::Layer {
    std::unique_ptr<QRhiTexture> y;
    std::unique_ptr<QRhiTexture> uv;
    std::shared_ptr<const VideoFrame> uploaded;
    bool hasContent = false;

    std::unique_ptr<QRhiTexture> rgb;
    std::unique_ptr<QRhiTextureRenderTarget> target;
    std::unique_ptr<QRhiRenderPassDescriptor> pass;
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
    m_compositeBindings.reset();
    m_compositeUniforms.reset();
    m_canvasPass.reset();
    m_canvasTarget.reset();
    m_canvas.reset();
    m_layerPipeline.reset();
    for (auto& layer : m_layers) {
        layer.reset();
    }
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
    m_compositeUniforms.reset(
        m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(QuadUniforms)));
    m_compositeUniforms->create();
    m_presentUniforms.reset(
        m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(QuadUniforms)));
    m_presentUniforms->create();

    for (auto& layer : m_layers) {
        layer = std::make_unique<Layer>();
        layer->uniforms.reset(
            m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kLayerUniformSize));
        layer->uniforms->create();
        // Texturas NV12 provisionales hasta el primer fotograma.
        layer->y.reset(m_rhi->newTexture(QRhiTexture::R8, QSize(16, 16)));
        layer->y->create();
        layer->uv.reset(m_rhi->newTexture(QRhiTexture::RG8, QSize(8, 8)));
        layer->uv->create();
    }
    createSizeDependent();
    ensurePipelines();
}

void Compositor::setCanvasSize(QSize size) {
    size = size.expandedTo(QSize(16, 16));
    if (size == m_canvasSize) {
        return;
    }
    m_canvasSize = size;
    if (m_rhi) {
        createSizeDependent();
    }
}

QRhiTexture* Compositor::canvas() const {
    return m_canvas.get();
}

void Compositor::createSizeDependent() {
    // Capas RGB y lienzo del tamaño del proyecto.
    for (auto& layer : m_layers) {
        layer->uploaded.reset(); // el ajuste al lienzo cambia: volver a subir el fotograma
        layer->target.reset();
        layer->rgb.reset(
            m_rhi->newTexture(QRhiTexture::RGBA8, m_canvasSize, 1, QRhiTexture::RenderTarget));
        layer->rgb->create();
        layer->target.reset(m_rhi->newTextureRenderTarget({layer->rgb.get()}));
        if (!layer->pass) {
            layer->pass.reset(layer->target->newCompatibleRenderPassDescriptor());
        }
        layer->target->setRenderPassDescriptor(layer->pass.get());
        layer->target->create();

        if (!layer->bindings) {
            layer->bindings.reset(m_rhi->newShaderResourceBindings());
        }
        layer->bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0,
                                                     QRhiShaderResourceBinding::VertexStage |
                                                         QRhiShaderResourceBinding::FragmentStage,
                                                     layer->uniforms.get()),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                      layer->y.get(), m_sampler.get()),
            QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage,
                                                      layer->uv.get(), m_sampler.get()),
        });
        layer->bindings->create();
    }

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
    if (!m_compositeBindings) {
        m_compositeBindings.reset(m_rhi->newShaderResourceBindings());
    }
    m_compositeBindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, stage, m_compositeUniforms.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  m_layers[0]->rgb.get(), m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage,
                                                  m_layers[1]->rgb.get(), m_sampler.get()),
    });
    m_compositeBindings->create();

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

void Compositor::ensurePipelines() {
    if (!m_layerPipeline) {
        m_layerPipeline.reset(m_rhi->newGraphicsPipeline());
        m_layerPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        m_layerPipeline->setShaderStages({
            {QRhiShaderStage::Vertex, loadShader("layer.vert")},
            {QRhiShaderStage::Fragment, loadShader("layer.frag")},
        });
        m_layerPipeline->setVertexInputLayout(quadLayout());
        m_layerPipeline->setShaderResourceBindings(m_layers[0]->bindings.get());
        m_layerPipeline->setRenderPassDescriptor(m_layers[0]->pass.get());
        m_layerPipeline->create();
    }
    if (!m_compositePipeline) {
        m_compositePipeline.reset(m_rhi->newGraphicsPipeline());
        m_compositePipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        m_compositePipeline->setShaderStages({
            {QRhiShaderStage::Vertex, loadShader("quad.vert")},
            {QRhiShaderStage::Fragment, loadShader("transition.frag")},
        });
        m_compositePipeline->setVertexInputLayout(quadLayout());
        m_compositePipeline->setShaderResourceBindings(m_compositeBindings.get());
        m_compositePipeline->setRenderPassDescriptor(m_canvasPass.get());
        m_compositePipeline->create();
    }
}

void Compositor::updateLayer(Layer& layer, const std::shared_ptr<const VideoFrame>& frame,
                             QRhiResourceUpdateBatch* updates) {
    layer.hasContent = static_cast<bool>(frame);
    if (!frame || frame == layer.uploaded) {
        return;
    }
    const QSize sizeY(frame->width, frame->height);
    if (layer.y->pixelSize() != sizeY) {
        layer.y.reset(m_rhi->newTexture(QRhiTexture::R8, sizeY));
        layer.y->create();
        layer.uv.reset(
            m_rhi->newTexture(QRhiTexture::RG8, QSize(frame->uvWidth(), frame->uvHeight())));
        layer.uv->create();
        // Nuevas texturas: se actualizan las asociaciones (mismo diseño).
        const auto stage =
            QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
        layer.bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, stage, layer.uniforms.get()),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                      layer.y.get(), m_sampler.get()),
            QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage,
                                                      layer.uv.get(), m_sampler.get()),
        });
        layer.bindings->create();
    }

    // Los datos se envían antes de que termine este fotograma: sin copia previa.
    QRhiTextureSubresourceUploadDescription y(QByteArray::fromRawData(
        reinterpret_cast<const char*>(frame->y.data()), static_cast<qsizetype>(frame->y.size())));
    y.setDataStride(static_cast<quint32>(frame->width));
    updates->uploadTexture(layer.y.get(), QRhiTextureUploadEntry(0, 0, y));
    QRhiTextureSubresourceUploadDescription uv(QByteArray::fromRawData(
        reinterpret_cast<const char*>(frame->uv.data()), static_cast<qsizetype>(frame->uv.size())));
    uv.setDataStride(static_cast<quint32>(frame->uvWidth() * 2));
    updates->uploadTexture(layer.uv.get(), QRhiTextureUploadEntry(0, 0, uv));

    // Ajuste al lienzo sin deformar (barras negras si la proporción no coincide).
    QMatrix4x4 transform = m_rhi->clipSpaceCorrMatrix();
    const QSizeF scale = fitScale(sizeY, m_canvasSize);
    transform.scale(static_cast<float>(scale.width()), static_cast<float>(scale.height()));
    const auto colors = yuvToRgbMatrix(frame->matrix, frame->fullRange);
    const QMatrix4x4 yuvToRgb(colors.data()); // por filas → se guarda por columnas, como GLSL
    updates->updateDynamicBuffer(layer.uniforms.get(), 0, 64, transform.constData());
    updates->updateDynamicBuffer(layer.uniforms.get(), 64, 64, yuvToRgb.constData());

    layer.uploaded = frame;
}

void Compositor::drawLayer(QRhiCommandBuffer* cb, Layer& layer, QRhiResourceUpdateBatch* updates) {
    cb->beginPass(layer.target.get(), Qt::black, {1.0f, 0}, updates);
    if (layer.hasContent && layer.uploaded) {
        cb->setGraphicsPipeline(m_layerPipeline.get());
        cb->setViewport({0, 0, static_cast<float>(m_canvasSize.width()),
                         static_cast<float>(m_canvasSize.height())});
        cb->setShaderResources(layer.bindings.get());
        const QRhiCommandBuffer::VertexInput input(m_quad.get(), 0);
        cb->setVertexInput(0, 1, &input);
        cb->draw(4);
    }
    cb->endPass();
}

void Compositor::render(QRhiCommandBuffer* cb, const CompositorInput& input) {
    if (!m_rhi) {
        return;
    }
    QRhiResourceUpdateBatch* updates = m_rhi->nextResourceUpdateBatch();
    if (!m_quadUploaded) {
        updates->uploadStaticBuffer(m_quad.get(), kQuad);
        m_quadUploaded = true;
    }

    const bool transitioning = input.transition.has_value() && input.b;
    updateLayer(*m_layers[0], input.a, updates);
    drawLayer(cb, *m_layers[0], updates);
    if (transitioning) {
        QRhiResourceUpdateBatch* updatesB = m_rhi->nextResourceUpdateBatch();
        updateLayer(*m_layers[1], input.b, updatesB);
        drawLayer(cb, *m_layers[1], updatesB);
    }

    // Composición en el lienzo.
    QuadUniforms u{};
    const QMatrix4x4 mvp = m_rhi->clipSpaceCorrMatrix();
    std::copy(mvp.constData(), mvp.constData() + 16, u.mvp);
    const int mode = transitioning ? static_cast<int>(input.transition->type) : -1;
    u.p0[0] = static_cast<float>(mode + 1);
    u.p0[1] = static_cast<float>(input.state.eased);
    u.p0[2] = static_cast<float>(input.state.velocity);
    u.p0[3] = transitioning ? static_cast<float>(input.transition->intensity) : 0.0f;
    u.p1[0] = transitioning ? static_cast<float>(input.transition->blur) : 0.0f;
    u.p1[1] = static_cast<float>(m_canvasSize.width()) / static_cast<float>(m_canvasSize.height());
    u.p1[2] = m_rhi->isYUpInFramebuffer() ? 1.0f : 0.0f;
    u.p1[3] = static_cast<float>(input.state.progress);
    QRhiResourceUpdateBatch* compositeUpdates = m_rhi->nextResourceUpdateBatch();
    compositeUpdates->updateDynamicBuffer(m_compositeUniforms.get(), 0, sizeof(u), &u);

    cb->beginPass(m_canvasTarget.get(), Qt::black, {1.0f, 0}, compositeUpdates);
    cb->setGraphicsPipeline(m_compositePipeline.get());
    cb->setViewport({0, 0, static_cast<float>(m_canvasSize.width()),
                     static_cast<float>(m_canvasSize.height())});
    cb->setShaderResources(m_compositeBindings.get());
    const QRhiCommandBuffer::VertexInput vertices(m_quad.get(), 0);
    cb->setVertexInput(0, 1, &vertices);
    cb->draw(4);
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
