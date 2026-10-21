#pragma once

// Seguimiento de movimiento (punto de anclaje): sigue una zona con textura de un video
// fotograma a fotograma, comparando la luminancia con correlación cruzada normalizada
// (insensible a cambios de brillo), primero a baja resolución y después afinando.

#include "media/video_frame.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sharkcut {

// Recuadro en el fotograma, en fracciones de su ancho y alto (0–1): esquina superior
// izquierda y tamaño. Así vale igual para el original y para su proxy.
struct TrackBox {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

// Dónde está la zona seguida en un fotograma: su centro (fracciones, 0–1) y lo
// parecida que es a la buscada (0–1; por debajo de kTrackLostScore se dio por perdida).
struct TrackPoint {
    double x = 0.0;
    double y = 0.0;
    double score = 0.0;
};

inline constexpr double kTrackLostScore = 0.5;

class MotionTracker {
public:
    // `first`: el fotograma donde se marcó la zona. Lanza std::invalid_argument si el
    // recuadro es demasiado pequeño o queda fuera del fotograma.
    MotionTracker(const VideoFrame& first, const TrackBox& box);

    // Busca la zona en el siguiente fotograma (cerca de donde estaba).
    TrackPoint track(const VideoFrame& frame);

    // Centro de la zona en el primer fotograma.
    TrackPoint start() const { return m_start; }

private:
    struct Image {
        int width = 0;
        int height = 0;
        std::vector<float> pixels;
        float at(int x, int y) const { return pixels[static_cast<std::size_t>(y) * width + x]; }
    };
    Image reduce(const VideoFrame& frame, int factor) const;
    static Image crop(const Image& image, int left, int top, int width, int height);
    // Mejor posición (esquina superior izquierda) de `patch` en `image` dentro de
    // [x0, x1] × [y0, y1]; devuelve la puntuación y, en `subX`/`subY`, el ajuste fino.
    static double match(const Image& image, const Image& patch, int x0, int y0, int x1, int y1,
                        int& bestX, int& bestY, double& subX, double& subY);

    int m_factor = 1;    // reducción de trabajo (el patrón mide ~kPatchSize px)
    Image m_template;    // a resolución de trabajo, adaptándose poco a poco
    Image m_original;    // el de la primera vez (para no derivar)
    double m_left = 0.0; // posición del patrón (esquina, px de trabajo)
    double m_top = 0.0;
    double m_vx = 0.0; // velocidad (px de trabajo por fotograma)
    double m_vy = 0.0;
    int m_frameWidth = 0;
    int m_frameHeight = 0;
    TrackPoint m_start;
};

// Sigue la zona `box` del video `path` (UTF-8), marcada en su fotograma `firstFrame`,
// hasta `lastFrame` (incluido; índices del video). `onPoint` recibe cada fotograma
// (índice y posición); si devuelve false, se para. Se para solo si la zona se pierde
// `maxLostFrames` fotogramas seguidos (esos ya no se entregan). `cancel` detiene todo.
// Lanza MediaError si el video no se puede leer.
void trackVideo(const std::string& path, std::int64_t firstFrame, std::int64_t lastFrame,
                const TrackBox& box,
                const std::function<bool(std::int64_t, const TrackPoint&)>& onPoint,
                int maxLostFrames = 12, const std::atomic<bool>* cancel = nullptr);

} // namespace sharkcut
