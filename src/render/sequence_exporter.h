#pragma once

#include "core/sequence.h"
#include "core/time.h"
#include "core/transition.h"

#include <QSize>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

class QOffscreenSurface;

namespace sharkcut {

struct ExportJob {
    SequenceLayout layout;
    FrameRate rate{60, 1};
    std::string pathA; // UTF-8
    std::string pathB;
    TransitionParams params;
    QSize canvasSize{1920, 1080};
    std::string outputPath;
    std::int64_t videoBitrate = 16'000'000;
    // Solo OpenGL: superficie auxiliar creada en el hilo de la interfaz
    // (QRhiGles2InitParams::newFallbackSurface). En Windows (Direct3D 11) no se usa.
    QOffscreenSurface* fallbackSurface = nullptr;
};

// Exporta la secuencia A → transición → B a MP4 en un hilo propio, componiendo cada
// fotograma con el mismo motor que la vista previa (sharkcut::Compositor).
//
// Las rellamadas se invocan desde el hilo de exportación.
class SequenceExporter {
public:
    struct Callbacks {
        std::function<void(std::int64_t done, std::int64_t total)> progress;
        // ok = false: error o cancelación (`message` lo explica). `encoder`: códec usado.
        std::function<void(bool ok, const std::string& message, const std::string& encoder)>
            finished;
    };

    SequenceExporter(ExportJob job, Callbacks callbacks);
    ~SequenceExporter(); // cancela y espera a que el hilo termine

    SequenceExporter(const SequenceExporter&) = delete;
    SequenceExporter& operator=(const SequenceExporter&) = delete;

    void cancel();

private:
    void run();

    const ExportJob m_job;
    const Callbacks m_callbacks;
    std::atomic<bool> m_cancel{false};
    std::thread m_thread; // último miembro: arranca cuando todo lo demás existe
};

} // namespace sharkcut
