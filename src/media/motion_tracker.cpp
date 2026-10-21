#include "media/motion_tracker.h"

#include "media/video_decoder.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sharkcut {

namespace {

// Lado (px de trabajo) al que se reduce la zona: suficiente detalle y rápido de buscar.
constexpr double kPatchSize = 40.0;
// Cuánto se adapta el patrón a lo que encuentra en cada fotograma (y cuánto vuelve al
// original): sigue los cambios lentos de forma o luz sin ir derivando.
constexpr float kAdapt = 0.10f;
constexpr float kOriginalPull = 0.05f;
constexpr double kAdaptScore = 0.8;

} // namespace

MotionTracker::Image MotionTracker::reduce(const VideoFrame& frame, int factor) const {
    Image image;
    image.width = std::max(1, frame.width / factor);
    image.height = std::max(1, frame.height / factor);
    image.pixels.resize(static_cast<std::size_t>(image.width) * image.height);
    const float scale = 1.0f / static_cast<float>(factor * factor);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            int sum = 0;
            for (int dy = 0; dy < factor; ++dy) {
                const std::uint8_t* row = frame.y.data() +
                                          static_cast<std::size_t>(y * factor + dy) * frame.width +
                                          static_cast<std::size_t>(x) * factor;
                for (int dx = 0; dx < factor; ++dx) {
                    sum += row[dx];
                }
            }
            image.pixels[static_cast<std::size_t>(y) * image.width + x] =
                static_cast<float>(sum) * scale;
        }
    }
    return image;
}

MotionTracker::Image MotionTracker::crop(const Image& image, int left, int top, int width,
                                         int height) {
    Image out;
    out.width = width;
    out.height = height;
    out.pixels.resize(static_cast<std::size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        const int sy = std::clamp(top + y, 0, image.height - 1);
        for (int x = 0; x < width; ++x) {
            const int sx = std::clamp(left + x, 0, image.width - 1);
            out.pixels[static_cast<std::size_t>(y) * width + x] = image.at(sx, sy);
        }
    }
    return out;
}

namespace {

// La mitad de resolución (para la búsqueda gruesa).
template <typename Image> Image half(const Image& image) {
    Image out;
    out.width = std::max(1, image.width / 2);
    out.height = std::max(1, image.height / 2);
    out.pixels.resize(static_cast<std::size_t>(out.width) * out.height);
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            const int x2 = std::min(2 * x + 1, image.width - 1);
            const int y2 = std::min(2 * y + 1, image.height - 1);
            out.pixels[static_cast<std::size_t>(y) * out.width + x] =
                0.25f * (image.at(2 * x, 2 * y) + image.at(x2, 2 * y) + image.at(2 * x, y2) +
                         image.at(x2, y2));
        }
    }
    return out;
}

// Pico de una parábola por tres puntos (−1, 0, 1): desplazamiento entre −0.5 y 0.5.
double parabola(double left, double center, double right) {
    const double d = left - 2.0 * center + right;
    if (d >= 0.0) {
        return 0.0;
    }
    return std::clamp(0.5 * (left - right) / d, -0.5, 0.5);
}

} // namespace

double MotionTracker::match(const Image& image, const Image& patch, int x0, int y0, int x1, int y1,
                            int& bestX, int& bestY, double& subX, double& subY) {
    const int pw = patch.width;
    const int ph = patch.height;
    x0 = std::clamp(x0, 0, image.width - pw);
    x1 = std::clamp(x1, 0, image.width - pw);
    y0 = std::clamp(y0, 0, image.height - ph);
    y1 = std::clamp(y1, 0, image.height - ph);
    const double n = static_cast<double>(pw) * ph;
    // Patrón con media cero y su norma.
    double mean = 0.0;
    for (float v : patch.pixels) {
        mean += v;
    }
    mean /= n;
    std::vector<float> zero(patch.pixels.size());
    double norm = 0.0;
    for (std::size_t i = 0; i < zero.size(); ++i) {
        zero[i] = static_cast<float>(patch.pixels[i] - mean);
        norm += static_cast<double>(zero[i]) * zero[i];
    }
    norm = std::sqrt(norm);

    // Sumas acumuladas de la zona de búsqueda (media y varianza de cada ventana).
    const int rw = x1 - x0 + pw;
    const int rh = y1 - y0 + ph;
    std::vector<double> sum(static_cast<std::size_t>(rw + 1) * (rh + 1), 0.0);
    std::vector<double> sumSq(sum.size(), 0.0);
    for (int y = 0; y < rh; ++y) {
        double row = 0.0;
        double rowSq = 0.0;
        for (int x = 0; x < rw; ++x) {
            const double v = image.at(x0 + x, y0 + y);
            row += v;
            rowSq += v * v;
            const std::size_t i = static_cast<std::size_t>(y + 1) * (rw + 1) + (x + 1);
            sum[i] = sum[i - (rw + 1)] + row;
            sumSq[i] = sumSq[i - (rw + 1)] + rowSq;
        }
    }
    const auto box = [&](const std::vector<double>& s, int x, int y) {
        const auto at = [&](int ax, int ay) {
            return s[static_cast<std::size_t>(ay) * (rw + 1) + ax];
        };
        return at(x + pw, y + ph) - at(x, y + ph) - at(x + pw, y) + at(x, y);
    };

    const int cols = x1 - x0 + 1;
    const int rows = y1 - y0 + 1;
    std::vector<double> scores(static_cast<std::size_t>(cols) * rows, -1.0);
    double best = -2.0;
    bestX = x0;
    bestY = y0;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            double cross = 0.0;
            for (int py = 0; py < ph; ++py) {
                const float* row =
                    &image.pixels[static_cast<std::size_t>(y + py) * image.width + x];
                const float* pat = &zero[static_cast<std::size_t>(py) * pw];
                float acc = 0.0f;
                for (int px = 0; px < pw; ++px) {
                    acc += row[px] * pat[px];
                }
                cross += acc;
            }
            const double s = box(sum, x - x0, y - y0);
            const double variance = box(sumSq, x - x0, y - y0) - s * s / n;
            const double score =
                variance > 1e-6 && norm > 1e-6 ? cross / (std::sqrt(variance) * norm) : 0.0;
            scores[static_cast<std::size_t>(y - y0) * cols + (x - x0)] = score;
            if (score > best) {
                best = score;
                bestX = x;
                bestY = y;
            }
        }
    }
    const auto scoreAt = [&](int x, int y) {
        if (x < x0 || x > x1 || y < y0 || y > y1) {
            return best;
        }
        return scores[static_cast<std::size_t>(y - y0) * cols + (x - x0)];
    };
    subX = parabola(scoreAt(bestX - 1, bestY), best, scoreAt(bestX + 1, bestY));
    subY = parabola(scoreAt(bestX, bestY - 1), best, scoreAt(bestX, bestY + 1));
    return best;
}

MotionTracker::MotionTracker(const VideoFrame& first, const TrackBox& box) {
    m_frameWidth = first.width;
    m_frameHeight = first.height;
    const double left = std::clamp(box.x, 0.0, 1.0) * first.width;
    const double top = std::clamp(box.y, 0.0, 1.0) * first.height;
    const double right = std::clamp(box.x + box.width, 0.0, 1.0) * first.width;
    const double bottom = std::clamp(box.y + box.height, 0.0, 1.0) * first.height;
    if (right - left < 8.0 || bottom - top < 8.0) {
        throw std::invalid_argument("La zona a seguir es demasiado pequeña");
    }
    m_factor = std::max(
        1, static_cast<int>(std::lround(std::max(right - left, bottom - top) / kPatchSize)));
    const Image image = reduce(first, m_factor);
    const int pw = std::max(4, static_cast<int>(std::lround((right - left) / m_factor)));
    const int ph = std::max(4, static_cast<int>(std::lround((bottom - top) / m_factor)));
    m_left = std::clamp(left / m_factor, 0.0, static_cast<double>(image.width - pw));
    m_top = std::clamp(top / m_factor, 0.0, static_cast<double>(image.height - ph));
    m_template = crop(image, static_cast<int>(std::lround(m_left)),
                      static_cast<int>(std::lround(m_top)), pw, ph);
    m_original = m_template;
    m_start = {(left + right) / 2.0 / first.width, (top + bottom) / 2.0 / first.height, 1.0};
}

TrackPoint MotionTracker::track(const VideoFrame& frame) {
    if (frame.width != m_frameWidth || frame.height != m_frameHeight) {
        return {0.0, 0.0, 0.0};
    }
    const Image image = reduce(frame, m_factor);
    const int pw = m_template.width;
    const int ph = m_template.height;
    const double predictedX = m_left + m_vx;
    const double predictedY = m_top + m_vy;
    // Se busca alrededor de donde debería estar (con la velocidad que llevaba).
    const int radius = static_cast<int>(std::max(pw, ph) * 0.75 + std::hypot(m_vx, m_vy)) + 4;

    int x = 0;
    int y = 0;
    double subX = 0.0;
    double subY = 0.0;
    double score = 0.0;
    if (pw >= 16 && ph >= 16) {
        // Gruesa a media resolución y después fina alrededor.
        const Image small = half(image);
        const Image smallPatch = half(m_template);
        const int cx = static_cast<int>(std::lround(predictedX / 2.0));
        const int cy = static_cast<int>(std::lround(predictedY / 2.0));
        const int r = radius / 2 + 1;
        int sx = 0;
        int sy = 0;
        match(small, smallPatch, cx - r, cy - r, cx + r, cy + r, sx, sy, subX, subY);
        score = match(image, m_template, 2 * sx - 2, 2 * sy - 2, 2 * sx + 2, 2 * sy + 2, x, y, subX,
                      subY);
    } else {
        const int cx = static_cast<int>(std::lround(predictedX));
        const int cy = static_cast<int>(std::lround(predictedY));
        score = match(image, m_template, cx - radius, cy - radius, cx + radius, cy + radius, x, y,
                      subX, subY);
    }

    if (score >= kTrackLostScore) {
        const double newLeft = x + subX;
        const double newTop = y + subY;
        // Velocidad suavizada (ayuda a buscar en el sitio correcto si se mueve rápido).
        m_vx = 0.5 * m_vx + 0.5 * (newLeft - m_left);
        m_vy = 0.5 * m_vy + 0.5 * (newTop - m_top);
        m_left = newLeft;
        m_top = newTop;
        if (score >= kAdaptScore) {
            const Image found = crop(image, x, y, pw, ph);
            for (std::size_t i = 0; i < m_template.pixels.size(); ++i) {
                m_template.pixels[i] = (1.0f - kAdapt - kOriginalPull) * m_template.pixels[i] +
                                       kAdapt * found.pixels[i] +
                                       kOriginalPull * m_original.pixels[i];
            }
        }
    } else {
        // Perdida (tapada, borrosa…): sigue donde debería estar y frena.
        m_left = predictedX;
        m_top = predictedY;
        m_vx *= 0.5;
        m_vy *= 0.5;
    }
    const double centerX = (m_left + pw / 2.0) * m_factor / m_frameWidth;
    const double centerY = (m_top + ph / 2.0) * m_factor / m_frameHeight;
    return {centerX, centerY, std::max(0.0, score)};
}

void trackVideo(const std::string& path, std::int64_t firstFrame, std::int64_t lastFrame,
                const TrackBox& box,
                const std::function<bool(std::int64_t, const TrackPoint&)>& onPoint,
                int maxLostFrames, const std::atomic<bool>* cancel) {
    VideoDecoder decoder(path, HwAccel::Auto);
    decoder.seekToFrame(firstFrame);
    const std::optional<VideoFrame> first = decoder.nextFrame();
    if (!first) {
        return;
    }
    MotionTracker tracker(*first, box);
    if (!onPoint(first->index, tracker.start())) {
        return;
    }
    // Los fotogramas en que se perdió se guardan hasta saber si vuelve a aparecer: entonces
    // se rellenan entre la última posición buena y la nueva.
    std::int64_t lastGoodIndex = first->index;
    TrackPoint lastGood = tracker.start();
    std::vector<std::int64_t> lost;
    while (!(cancel && cancel->load())) {
        const std::optional<VideoFrame> frame = decoder.nextFrame();
        if (!frame || frame->index > lastFrame) {
            return;
        }
        const TrackPoint point = tracker.track(*frame);
        if (point.score < kTrackLostScore) {
            lost.push_back(frame->index);
            if (static_cast<int>(lost.size()) > maxLostFrames) {
                return;
            }
            continue;
        }
        for (std::int64_t index : lost) {
            const double t = static_cast<double>(index - lastGoodIndex) /
                             static_cast<double>(frame->index - lastGoodIndex);
            const TrackPoint filled{lastGood.x + t * (point.x - lastGood.x),
                                    lastGood.y + t * (point.y - lastGood.y), 0.0};
            if (!onPoint(index, filled)) {
                return;
            }
        }
        lost.clear();
        if (!onPoint(frame->index, point)) {
            return;
        }
        lastGood = point;
        lastGoodIndex = frame->index;
    }
}

} // namespace sharkcut
