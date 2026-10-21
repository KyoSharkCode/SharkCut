#include "render/caption_renderer.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace sharkcut {

namespace {

constexpr std::size_t kCacheSize = 48;

QColor rgb(std::uint32_t color, double alpha = 1.0) {
    QColor c(static_cast<int>((color >> 16) & 0xFF), static_cast<int>((color >> 8) & 0xFF),
             static_cast<int>(color & 0xFF));
    c.setAlphaF(static_cast<float>(std::clamp(alpha, 0.0, 1.0)));
    return c;
}

// Parte el texto en líneas que caben en `maxWidth` (los saltos de línea se respetan).
QStringList wrap(const QString& text, const QFontMetricsF& metrics, double maxWidth) {
    QStringList lines;
    for (const QString& paragraph : text.split(QLatin1Char('\n'))) {
        QString line;
        for (const QString& word : paragraph.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            const QString candidate = line.isEmpty() ? word : line + QLatin1Char(' ') + word;
            if (!line.isEmpty() && metrics.horizontalAdvance(candidate) > maxWidth) {
                lines << line;
                line = word;
            } else {
                line = candidate;
            }
        }
        lines << line;
    }
    while (!lines.isEmpty() && lines.back().trimmed().isEmpty()) {
        lines.removeLast();
    }
    return lines;
}

std::string keyOf(const std::string& text, const CaptionStyle& s, QSize canvas) {
    std::string key = text;
    key += '\x1F' + s.font;
    for (const double v : {s.size, s.outline, s.shadow, s.box, s.x, s.y, s.maxWidth}) {
        key += '\x1F' + std::to_string(v);
    }
    for (const std::uint32_t v : {s.color, s.outlineColor, s.shadowColor, s.boxColor}) {
        key += '\x1F' + std::to_string(v);
    }
    key += s.bold ? 'B' : '-';
    key += s.italic ? 'I' : '-';
    key += s.uppercase ? 'U' : '-';
    key += '\x1F' + std::to_string(canvas.width()) + 'x' + std::to_string(canvas.height());
    return key;
}

} // namespace

double captionScale(QSize canvas) {
    return std::max(1, std::min(canvas.width(), canvas.height())) / 1080.0;
}

CompositorOverlay renderCaption(const std::string& utf8, const CaptionStyle& style, QSize canvas) {
    QString text = QString::fromStdString(utf8).trimmed();
    if (style.uppercase) {
        text = text.toUpper();
    }
    if (text.isEmpty() || canvas.isEmpty()) {
        return {};
    }
    const double scale = captionScale(canvas);
    QFont font(QString::fromStdString(style.font));
    font.setPixelSize(std::max(1, static_cast<int>(std::lround(style.size * scale))));
    font.setBold(style.bold);
    font.setItalic(style.italic);
    const QFontMetricsF metrics(font);

    const QStringList lines = wrap(text, metrics, style.maxWidth * canvas.width());
    if (lines.isEmpty()) {
        return {};
    }
    // Contorno del texto, cada línea centrada.
    const double lineHeight = metrics.lineSpacing();
    QPainterPath path;
    double widest = 0.0;
    for (const QString& line : lines) {
        widest = std::max(widest, metrics.horizontalAdvance(line));
    }
    for (int i = 0; i < lines.size(); ++i) {
        const double x = (widest - metrics.horizontalAdvance(lines[i])) / 2.0;
        path.addText(x, metrics.ascent() + i * lineHeight, font, lines[i]);
    }
    const QSizeF textSize(widest, lineHeight * lines.size());

    const double outline = style.outline * scale;
    const double boxPad = style.box > 0.0 ? style.size * scale * 0.3 : 0.0;
    const double shadowOffset = style.shadow > 0.0 ? std::max(2.0, style.size * scale * 0.06) : 0.0;
    const double margin = std::ceil(std::max(outline, boxPad) + shadowOffset + 2.0);
    const QSize imageSize(static_cast<int>(std::ceil(textSize.width() + 2 * margin)),
                          static_cast<int>(std::ceil(textSize.height() + 2 * margin)));

    auto image = std::make_shared<QImage>(imageSize, QImage::Format_RGBA8888_Premultiplied);
    image->fill(Qt::transparent);
    {
        QPainter p(image.get());
        p.setRenderHint(QPainter::Antialiasing);
        p.translate(margin, margin);
        if (style.box > 0.0) {
            p.setPen(Qt::NoPen);
            p.setBrush(rgb(style.boxColor, style.box));
            p.drawRoundedRect(QRectF(QPointF(-boxPad, -boxPad * 0.6),
                                     textSize + QSizeF(2 * boxPad, 1.2 * boxPad)),
                              boxPad * 0.6, boxPad * 0.6);
        }
        const auto drawText = [&](const QPainterPath& shape, const QColor& fill,
                                  const QColor& stroke) {
            if (outline > 0.0) {
                QPen pen(stroke, outline * 2.0);
                pen.setJoinStyle(Qt::RoundJoin);
                p.strokePath(shape, pen);
            }
            p.fillPath(shape, fill);
        };
        if (style.shadow > 0.0) {
            const QColor shadow = rgb(style.shadowColor, style.shadow);
            drawText(path.translated(shadowOffset, shadowOffset), shadow, shadow);
        }
        drawText(path, rgb(style.color), rgb(style.outlineColor));
    }

    // Centrado en (x, y) del lienzo, sin salirse de él.
    const double w = imageSize.width();
    const double h = imageSize.height();
    const double cx =
        std::clamp(style.x * canvas.width(), w / 2, std::max(w / 2, canvas.width() - w / 2));
    const double cy =
        std::clamp(style.y * canvas.height(), h / 2, std::max(h / 2, canvas.height() - h / 2));
    return {std::move(image), QRectF(cx - w / 2, cy - h / 2, w, h)};
}

CompositorOverlay CaptionImageCache::get(const std::string& text, const CaptionStyle& style,
                                         QSize canvas) {
    const std::string key = keyOf(text, style, canvas);
    if (const auto it = m_items.find(key); it != m_items.end()) {
        return it->second;
    }
    CompositorOverlay rendered = renderCaption(text, style, canvas);
    m_items.emplace(key, rendered);
    m_order.push_back(key);
    if (m_order.size() > kCacheSize) {
        m_items.erase(m_order.front());
        m_order.erase(m_order.begin());
    }
    return rendered;
}

std::vector<CompositorOverlay> CaptionImageCache::overlaysAt(const Project& project,
                                                             std::int64_t frame, QSize canvas,
                                                             const StyleOverride& override) {
    std::vector<CompositorOverlay> result;
    for (std::size_t t = 0; t < project.captionTracks.size(); ++t) {
        if (project.captionTracks[t].hidden) {
            continue;
        }
        if (const Caption* c = project.captionAt(t, frame)) {
            const CaptionStyle* provisional = override ? override(t, *c) : nullptr;
            CompositorOverlay o =
                get(c->text, provisional ? *provisional : project.styleOf(t, *c), canvas);
            if (o.image) {
                result.push_back(std::move(o));
            }
        }
    }
    return result;
}

} // namespace sharkcut
