#include "render/text_frame.h"

#include <algorithm>
#include <cmath>

namespace sharkcut {

namespace {

constexpr std::size_t kMaxFrames = 24;

std::uint8_t code(double v) {
    return static_cast<std::uint8_t>(std::clamp(std::lround(v * 255.0), 0L, 255L));
}

} // namespace

VideoFrame textImageToFrame(const QImage& source, QRectF rect, QSize canvas) {
    VideoFrame out;
    out.width = std::max(2, canvas.width() & ~1);
    out.height = std::max(2, canvas.height() & ~1);
    out.matrix = ColorMatrix::Bt709;
    out.fullRange = true;
    const auto w = static_cast<std::size_t>(out.width);
    const auto h = static_cast<std::size_t>(out.height);
    out.y.assign(w * h, 0);
    out.uv.assign(static_cast<std::size_t>(out.uvWidth()) * 2 * out.uvHeight(), 128);
    out.a.assign(w * h, 0);

    const QImage image = source.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    const int left = static_cast<int>(std::lround(rect.left()));
    const int top = static_cast<int>(std::lround(rect.top()));
    // Color sin premultiplicar de cada píxel (para el promedio de la crominancia).
    std::vector<float> cb(w * h, 0.0f);
    std::vector<float> cr(w * h, 0.0f);
    for (int iy = 0; iy < image.height(); ++iy) {
        const int y = top + iy;
        if (y < 0 || y >= out.height) {
            continue;
        }
        const uchar* row = image.constScanLine(iy);
        for (int ix = 0; ix < image.width(); ++ix) {
            const int x = left + ix;
            if (x < 0 || x >= out.width) {
                continue;
            }
            const uchar* px = row + static_cast<std::ptrdiff_t>(ix) * 4;
            const int a = px[3];
            if (a == 0) {
                continue;
            }
            const double alpha = a / 255.0;
            const double r = px[0] / 255.0 / alpha;
            const double g = px[1] / 255.0 / alpha;
            const double b = px[2] / 255.0 / alpha;
            const double luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            const std::size_t i = static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x);
            out.y[i] = code(luma);
            out.a[i] = static_cast<std::uint8_t>(a);
            cb[i] = static_cast<float>((b - luma) / 1.8556);
            cr[i] = static_cast<float>((r - luma) / 1.5748);
        }
    }
    // Crominancia de cada bloque de 2×2, pesada por la transparencia.
    const std::size_t uvW = static_cast<std::size_t>(out.uvWidth());
    for (std::size_t by = 0; by + 1 < h + 1 && by < h; by += 2) {
        for (std::size_t bx = 0; bx < w; bx += 2) {
            double sum = 0.0;
            double sumCb = 0.0;
            double sumCr = 0.0;
            for (std::size_t dy = 0; dy < 2 && by + dy < h; ++dy) {
                for (std::size_t dx = 0; dx < 2 && bx + dx < w; ++dx) {
                    const std::size_t i = (by + dy) * w + bx + dx;
                    const double weight = out.a[i];
                    sum += weight;
                    sumCb += cb[i] * weight;
                    sumCr += cr[i] * weight;
                }
            }
            if (sum > 0.0) {
                const std::size_t j = (by / 2) * uvW * 2 + (bx / 2) * 2;
                out.uv[j] = code(sumCb / sum + 128.0 / 255.0);
                out.uv[j + 1] = code(sumCr / sum + 128.0 / 255.0);
            }
        }
    }
    return out;
}

TextFrameCache::Entry TextFrameCache::get(const TextClip& text, QSize canvas) {
    CaptionStyle style = text.style;
    style.x = 0.5; // en el centro: se coloca con la transformación del clip
    style.y = 0.5;
    const CompositorOverlay overlay = m_images.get(text.text, style, canvas);
    if (!overlay.image) {
        return {};
    }
    for (const auto& [image, entry] : m_frames) {
        if (image == overlay.image) {
            return entry;
        }
    }
    Entry entry;
    entry.frame =
        std::make_shared<const VideoFrame>(textImageToFrame(*overlay.image, overlay.rect, canvas));
    entry.rect = overlay.rect;
    m_frames.emplace_back(overlay.image, entry);
    if (m_frames.size() > kMaxFrames) {
        m_frames.erase(m_frames.begin());
    }
    return entry;
}

} // namespace sharkcut
