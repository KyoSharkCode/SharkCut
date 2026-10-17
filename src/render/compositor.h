#pragma once

#include "core/project.h"
#include "core/transition.h"
#include "media/video_frame.h"

#include <QColor>
#include <QImage>
#include <QRectF>
#include <QSize>

#include <memory>
#include <optional>
#include <vector>

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

// Una capa del lienzo (una pista de video): un clip, o dos con una transición entre
// ellos, colocados según `transform`.
struct CompositorLayer {
    std::shared_ptr<const VideoFrame> a; // clip principal (o saliente)
    std::shared_ptr<const VideoFrame> b; // clip entrante (solo durante una transición)
    std::optional<TransitionParams> transition;
    TransitionState state;
    ClipTransform transform;  // de `a`
    ClipTransform transformB; // de `b`
};

// Imagen ya dibujada (un subtítulo) que va encima de las capas, en `rect` (píxeles del
// lienzo). La imagen es RGBA8888 con alfa premultiplicado.
struct CompositorOverlay {
    std::shared_ptr<const QImage> image;
    QRectF rect;
};

// Lo que hay que dibujar en un fotograma: las capas, de abajo arriba, sobre negro, y
// encima las superposiciones.
struct CompositorInput {
    std::vector<CompositorLayer> layers;
    std::vector<CompositorOverlay> overlays;
};

// Motor de composición en la GPU (QRhi). Es el mismo para la vista previa y para la
// exportación, así lo que se ve es lo que se exporta.
//
// Cada capa sin transición se dibuja directamente en el lienzo (NV12 → RGB con su
// posición, escala, recorte y opacidad). Una capa con transición dibuja antes sus dos
// clips en texturas del tamaño del lienzo y la transición las mezcla en el lienzo.
// Al final, el lienzo se presenta en el destino (visor).
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
    struct Source;
    struct TransitionTargets;
    struct Overlay;
    Overlay& overlay(std::size_t index);
    // Sube la imagen (si cambió) y prepara los uniformes para dibujarla en su sitio.
    void prepareOverlay(Overlay& overlay, const CompositorOverlay& input,
                        QRhiResourceUpdateBatch* updates);
    void releaseAll();
    void createCanvas();
    void createTransitionTargets(TransitionTargets& targets);
    void ensurePipelines();
    Source& source(std::size_t index);
    TransitionTargets& transitionTargets(std::size_t index);
    void bindSource(Source& source); // texturas y uniformes de la fuente
    // Sube el fotograma (si cambió) y prepara los uniformes para dibujarlo con `transform`.
    void prepareSource(Source& source, const VideoFrame& frame, const ClipTransform& transform,
                       double opacity, QRhiResourceUpdateBatch* updates);
    void drawSource(QRhiCommandBuffer* cb, Source& source, QRhiGraphicsPipeline* pipeline);

    QRhi* m_rhi = nullptr;
    QSize m_canvasSize{1920, 1080};

    std::unique_ptr<QRhiBuffer> m_quad;
    bool m_quadUploaded = false;
    std::unique_ptr<QRhiSampler> m_sampler;

    std::vector<std::unique_ptr<Source>> m_sources;
    std::vector<std::unique_ptr<TransitionTargets>> m_transitions;
    std::unique_ptr<QRhiGraphicsPipeline> m_layerPipeline;     // a una textura de transición
    std::unique_ptr<QRhiGraphicsPipeline> m_blendPipeline;     // directo al lienzo, con mezcla
    std::unique_ptr<QRhiGraphicsPipeline> m_compositePipeline; // transición → lienzo
    std::vector<std::unique_ptr<Overlay>> m_overlays;
    std::unique_ptr<QRhiGraphicsPipeline> m_overlayPipeline; // subtítulos → lienzo

    std::unique_ptr<QRhiTexture> m_canvas;
    std::unique_ptr<QRhiTextureRenderTarget> m_canvasTarget;
    std::unique_ptr<QRhiRenderPassDescriptor> m_canvasPass;

    std::unique_ptr<QRhiBuffer> m_presentUniforms;
    std::unique_ptr<QRhiShaderResourceBindings> m_presentBindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_presentPipeline;
    QRhiRenderPassDescriptor* m_presentPass = nullptr; // del destino actual (no propio)
};

} // namespace sharkcut
