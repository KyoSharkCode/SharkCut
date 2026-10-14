#include "videoviewer.h"

#include "compositionsource.h"
#include "render/compositor.h"

#include <rhi/qrhi.h>

namespace {

class VideoRenderer : public QQuickRhiItemRenderer {
public:
    void initialize(QRhiCommandBuffer*) override { m_compositor.initialize(rhi()); }

    void synchronize(QQuickRhiItem* item) override {
        auto* viewer = static_cast<VideoViewer*>(item);
        m_background = viewer->backgroundColor();
        if (CompositionSource* source = viewer->compositionSource()) {
            m_input = source->composition();
            m_compositor.setCanvasSize(source->canvasSize());
        } else {
            m_input = {};
        }
    }

    void render(QRhiCommandBuffer* cb) override {
        m_compositor.render(cb, m_input);
        m_compositor.present(cb, renderTarget(), m_background);
    }

private:
    sharkcut::Compositor m_compositor;
    sharkcut::CompositorInput m_input;
    QColor m_background = Qt::black;
};

} // namespace

VideoViewer::VideoViewer(QQuickItem* parent) : QQuickRhiItem(parent) {
    setAlphaBlending(false);
}

QObject* VideoViewer::source() const {
    return m_source;
}

void VideoViewer::setSource(QObject* source) {
    auto* composition = qobject_cast<CompositionSource*>(source);
    if (m_source == composition) {
        return;
    }
    if (m_source) {
        disconnect(m_source, nullptr, this, nullptr);
    }
    m_source = composition;
    if (m_source) {
        connect(m_source, &CompositionSource::compositionChanged, this, &QQuickItem::update);
    }
    emit sourceChanged();
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
