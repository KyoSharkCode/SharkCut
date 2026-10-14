#pragma once

#include "core/export_settings.h"
#include "core/project.h"
#include "core/sequence.h"
#include "core/time.h"
#include "core/transition.h"

#include <QSize>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <variant>

class QOffscreenSurface;

namespace sharkcut {

// Secuencia de prueba de la fase 1: clip A → transición → clip B.
struct TransitionTest {
    SequenceLayout layout;
    std::string pathA; // UTF-8
    std::string pathB;
    TransitionParams params;
};

struct ExportJob {
    // Lo que se exporta: la prueba de transición o la línea de tiempo de un proyecto
    // (una copia: las ediciones posteriores no le afectan).
    std::variant<TransitionTest, Project> content;
    // Fps del archivo. En la prueba de transición son también los de la secuencia; la
    // línea de tiempo puede exportarse a otros (se saltan o repiten fotogramas).
    FrameRate rate{60, 1};
    // Tamaño del archivo. Si no es el lienzo del proyecto, debe tener su misma forma: se
    // compone directamente a ese tamaño.
    QSize canvasSize{1920, 1080};
    // Tramo [rangeStart, rangeEnd) en fotogramas de la secuencia (-1: hasta el final).
    std::int64_t rangeStart = 0;
    std::int64_t rangeEnd = -1;
    std::string outputPath; // UTF-8
    // false: si ya existe un archivo con ese nombre se usa "nombre (2).mp4", "(3)"…
    bool overwrite = false;
    VideoCodec codec = VideoCodec::H264;
    std::int64_t videoBitrate = 16'000'000;
    bool audio = true; // false: el archivo sale sin audio
    int audioBitrate = 192'000;
    // Solo OpenGL: superficie auxiliar creada en el hilo de la interfaz
    // (QRhiGles2InitParams::newFallbackSurface). En Windows (Direct3D 11) no se usa.
    QOffscreenSurface* fallbackSurface = nullptr;
};

// Exporta a MP4 (H.264 o H.265) en un hilo propio, componiendo cada fotograma con el mismo motor
// que la vista previa (sharkcut::Compositor). Los archivos se abren dentro de ese hilo.
//
// Las rellamadas se invocan desde el hilo de exportación.
class SequenceExporter {
public:
    struct Callbacks {
        std::function<void(std::int64_t done, std::int64_t total)> progress;
        // ok = false: error o cancelación (`message` lo explica). `encoder`: códec usado;
        // `path`: el archivo escrito (UTF-8; puede llevar " (2)" si el nombre existía).
        std::function<void(bool ok, const std::string& message, const std::string& encoder,
                           const std::string& path)>
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
