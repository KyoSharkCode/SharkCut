#include "videoviewer.h"

#include "media/color.h"
#include "player.h"

#include <QFile>
#include <QMatrix4x4>
#include <rhi/qrhi.h>

#include <algorithm>
#include <memory>

namespace {

QShader loadShader(const QString& name) {
    QFile file(name);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("No se pudo cargar el shader %s", qPrintable(name));
        return {};
    }
    return QShader::fromSerialized(file.readAll());
}

// Posición (x, y) y coordenada de textura (u, v) de las 4 esquinas.
// v = 0 es la primera fila del fotograma (la de arriba) en todas las API.
constexpr float kQuad[] = {
    -1.0f, -1.0f, 0.0f, 1.0f, //
    1.0f,  -1.0f, 1.0f, 1.0f, //
    -1.0f, 1.0f,  0.0f, 0.0f, //
    1.0f,  1.0f,  1.0f, 0.0f, //
};

constexpr quint32 kUniformSize = 2 * 64; // dos mat4

class VideoRenderer : public QQuickRhiItemRenderer {
public:
    void initialize(QRhiCommandBuffer* cb) override;
    void synchronize(QQuickRhiItem* item) override;
    void render(QRhiCommandBuffer* cb) override;

private:
    void ensureTextures(int width, int height);
    void uploadFrame(QRhiResourceUpdateBatch* updates);

    QRhi* m_rhi = nullptr;
    int m_sampleCount = 1;

    std::unique_ptr<QRhiBuffer> m_vertexBuffer;
    std::unique_ptr<QRhiBuffer> m_uniformBuffer;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiTexture> m_textureY;
    std::unique_ptr<QRhiTexture> m_textureUV;
    std::unique_ptr<QRhiShaderResourceBindings> m_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    bool m_vertexDataUploaded = false;

    std::shared_ptr<const sharkcut::VideoFrame> m_frame;    // el que hay que mostrar
    std::shared_ptr<const sharkcut::VideoFrame> m_uploaded; // el que está en la GPU
    QColor m_backgroundColor = Qt::black;
};

void VideoRenderer::initialize(QRhiCommandBuffer*) {
    if (m_rhi != rhi()) {
        // Nuevo dispositivo gráfico: se recrea todo.
        m_pipeline.reset();
        m_bindings.reset();
        m_textureY.reset();
        m_textureUV.reset();
        m_sampler.reset();
        m_uniformBuffer.reset();
        m_vertexBuffer.reset();
        m_vertexDataUploaded = false;
        m_uploaded.reset();
        m_rhi = rhi();
    }
    if (m_sampleCount != renderTarget()->sampleCount()) {
        m_pipeline.reset();
        m_sampleCount = renderTarget()->sampleCount();
    }

    if (!m_vertexBuffer) {
        m_vertexBuffer.reset(
            m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(kQuad)));
        m_vertexBuffer->create();
        m_uniformBuffer.reset(
            m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kUniformSize));
        m_uniformBuffer->create();
        m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                          QRhiSampler::None, QRhiSampler::ClampToEdge,
                                          QRhiSampler::ClampToEdge));
        m_sampler->create();
        ensureTextures(16, 16); // texturas provisionales hasta el primer fotograma
    }

    if (!m_pipeline) {
        m_pipeline.reset(m_rhi->newGraphicsPipeline());
        m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        m_pipeline->setSampleCount(m_sampleCount);
        m_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/shaders/video.vert.qsb"))},
            {QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/video.frag.qsb"))},
        });
        QRhiVertexInputLayout layout;
        layout.setBindings({{4 * sizeof(float)}});
        layout.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float2, 0},
            {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)},
        });
        m_pipeline->setVertexInputLayout(layout);
        m_pipeline->setShaderResourceBindings(m_bindings.get());
        m_pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        m_pipeline->create();
    }
}

void VideoRenderer::ensureTextures(int width, int height) {
    const QSize sizeY(width, height);
    const QSize sizeUV((width + 1) / 2, (height + 1) / 2);
    if (m_textureY && m_textureY->pixelSize() == sizeY) {
        return;
    }
    m_textureY.reset(m_rhi->newTexture(QRhiTexture::R8, sizeY));
    m_textureY->create();
    m_textureUV.reset(m_rhi->newTexture(QRhiTexture::RG8, sizeUV));
    m_textureUV->create();

    // Las texturas cambiaron: se actualizan las asociaciones en el mismo objeto, que
    // es el que conoce el pipeline (el diseño no cambia, así que sigue siendo válido).
    if (!m_bindings) {
        m_bindings.reset(m_rhi->newShaderResourceBindings());
    }
    m_bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_uniformBuffer.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  m_textureY.get(), m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage,
                                                  m_textureUV.get(), m_sampler.get()),
    });
    m_bindings->create();
}

void VideoRenderer::synchronize(QQuickRhiItem* item) {
    auto* viewer = static_cast<VideoViewer*>(item);
    m_backgroundColor = viewer->backgroundColor();
    m_frame = viewer->player() ? viewer->player()->videoFrame() : nullptr;
}

void VideoRenderer::uploadFrame(QRhiResourceUpdateBatch* updates) {
    const sharkcut::VideoFrame& f = *m_frame;
    ensureTextures(f.width, f.height);

    // Los datos se envían a la GPU antes de que termine render(): no hace falta copiarlos.
    QRhiTextureSubresourceUploadDescription y(QByteArray::fromRawData(
        reinterpret_cast<const char*>(f.y.data()), static_cast<qsizetype>(f.y.size())));
    y.setDataStride(static_cast<quint32>(f.width));
    updates->uploadTexture(m_textureY.get(), QRhiTextureUploadEntry(0, 0, y));

    QRhiTextureSubresourceUploadDescription uv(QByteArray::fromRawData(
        reinterpret_cast<const char*>(f.uv.data()), static_cast<qsizetype>(f.uv.size())));
    uv.setDataStride(static_cast<quint32>(f.uvWidth() * 2));
    updates->uploadTexture(m_textureUV.get(), QRhiTextureUploadEntry(0, 0, uv));

    m_uploaded = m_frame;
}

void VideoRenderer::render(QRhiCommandBuffer* cb) {
    QRhiResourceUpdateBatch* updates = m_rhi->nextResourceUpdateBatch();
    if (!m_vertexDataUploaded) {
        updates->uploadStaticBuffer(m_vertexBuffer.get(), kQuad);
        m_vertexDataUploaded = true;
    }
    if (m_frame && m_frame != m_uploaded) {
        uploadFrame(updates);
    }

    const QSize outputSize = renderTarget()->pixelSize();
    const bool hasVideo =
        m_uploaded && m_frame && outputSize.width() > 0 && outputSize.height() > 0;
    if (hasVideo) {
        // Ajuste de aspecto: el video ocupa el máximo posible sin deformarse.
        const double videoAspect =
            static_cast<double>(m_uploaded->width) / std::max(m_uploaded->height, 1);
        const double outputAspect =
            static_cast<double>(outputSize.width()) / std::max(outputSize.height(), 1);
        QMatrix4x4 transform = m_rhi->clipSpaceCorrMatrix();
        if (videoAspect > outputAspect) {
            transform.scale(1.0f, static_cast<float>(outputAspect / videoAspect));
        } else {
            transform.scale(static_cast<float>(videoAspect / outputAspect), 1.0f);
        }
        // QMatrix4x4 recibe la matriz por filas y la guarda por columnas, como GLSL.
        const auto colors = sharkcut::yuvToRgbMatrix(m_uploaded->matrix, m_uploaded->fullRange);
        const QMatrix4x4 yuvToRgb(colors.data());
        updates->updateDynamicBuffer(m_uniformBuffer.get(), 0, 64, transform.constData());
        updates->updateDynamicBuffer(m_uniformBuffer.get(), 64, 64, yuvToRgb.constData());
    }

    cb->beginPass(renderTarget(), m_backgroundColor, {1.0f, 0}, updates);
    if (hasVideo) {
        cb->setGraphicsPipeline(m_pipeline.get());
        cb->setViewport({0, 0, static_cast<float>(outputSize.width()),
                         static_cast<float>(outputSize.height())});
        cb->setShaderResources(m_bindings.get());
        const QRhiCommandBuffer::VertexInput input(m_vertexBuffer.get(), 0);
        cb->setVertexInput(0, 1, &input);
        cb->draw(4);
    }
    cb->endPass();
}

} // namespace

VideoViewer::VideoViewer(QQuickItem* parent) : QQuickRhiItem(parent) {
    setAlphaBlending(false);
}

void VideoViewer::setPlayer(Player* player) {
    if (m_player == player) {
        return;
    }
    if (m_player) {
        disconnect(m_player, nullptr, this, nullptr);
    }
    m_player = player;
    if (m_player) {
        connect(m_player, &Player::frameChanged, this, &QQuickItem::update);
    }
    emit playerChanged();
    update();
}

void VideoViewer::setBackgroundColor(const QColor& color) {
    if (m_backgroundColor != color) {
        m_backgroundColor = color;
        emit backgroundColorChanged();
        update();
    }
}

QQuickRhiItemRenderer* VideoViewer::createRenderer() {
    return new VideoRenderer;
}
