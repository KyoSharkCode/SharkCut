#pragma once

#include "core/transition.h"
#include "media/video_frame.h"

#include <QColor>
#include <QSize>

#include <array>
#include <memory>
#include <optional>

class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiRenderTarget;
class QRhiResourceUpdateBatch;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;
class QRhiTextureRenderTarget;

namespace sharkcut {

// Lo que hay que dibujar en un fotograma: uno o dos clips y, si hay dos, la
// transición entre ellos.
struct CompositorInput {
    std::shared_ptr<const VideoFrame> a; // clip principal (o saliente)
    std::shared_ptr<const VideoFrame> b; // clip entrante (solo durante una transición)
    std::optional<TransitionParams> transition;
    TransitionState state;
};

// Motor de composición en la GPU (QRhi). Es el mismo para la vista previa y para la
// exportación, así lo que se ve es lo que se exporta.
//
// Pasadas: cada clip NV12 → capa RGB del tamaño del lienzo (ajustada sin deformar);
// las capas → lienzo, aplicando la transición; el lienzo → destino (visor).
class Compositor {
public:
    Compositor();
    ~Compositor();

    Compositor(const Compositor&) = delete;
    Compositor& operator=(const Compositor&) = delete;

    // Prepara los recursos para `rhi` (si cambia de dispositivo, se recrean).
    void initialize(QRhi* rhi);

    // Tamaño del lienzo (resolución del proyecto), p. ej. 1920×1080.
    void setCanvasSize(QSize size);
    QSize canvasSize() const { return m_canvasSize; }

    // Compone `input` en el lienzo. Graba las pasadas en `cb`.
    void render(QRhiCommandBuffer* cb, const CompositorInput& input);

    // Dibuja el lienzo en `target` ajustado a su tamaño, con `background` alrededor.
    void present(QRhiCommandBuffer* cb, QRhiRenderTarget* target, const QColor& background);

    QRhiTexture* canvas() const;

private:
    struct Layer;
    void releaseAll();
    void createSizeDependent();
    void ensurePipelines();
    void updateLayer(Layer& layer, const std::shared_ptr<const VideoFrame>& frame,
                     QRhiResourceUpdateBatch* updates);
    void drawLayer(QRhiCommandBuffer* cb, Layer& layer, QRhiResourceUpdateBatch* updates);

    QRhi* m_rhi = nullptr;
    QSize m_canvasSize{1920, 1080};

    std::unique_ptr<QRhiBuffer> m_quad;
    bool m_quadUploaded = false;
    std::unique_ptr<QRhiSampler> m_sampler;

    std::array<std::unique_ptr<Layer>, 2> m_layers;
    std::unique_ptr<QRhiGraphicsPipeline> m_layerPipeline;

    std::unique_ptr<QRhiTexture> m_canvas;
    std::unique_ptr<QRhiTextureRenderTarget> m_canvasTarget;
    std::unique_ptr<QRhiRenderPassDescriptor> m_canvasPass;
    std::unique_ptr<QRhiBuffer> m_compositeUniforms;
    std::unique_ptr<QRhiShaderResourceBindings> m_compositeBindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_compositePipeline;

    std::unique_ptr<QRhiBuffer> m_presentUniforms;
    std::unique_ptr<QRhiShaderResourceBindings> m_presentBindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_presentPipeline;
    QRhiRenderPassDescriptor* m_presentPass = nullptr; // del destino actual (no propio)
};

} // namespace sharkcut
