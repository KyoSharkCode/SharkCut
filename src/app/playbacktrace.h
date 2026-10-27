#pragma once

#include <QElapsedTimer>
#include <QString>
#include <QtGlobal>

#include <functional>
#include <map>
#include <utility>
#include <vector>

// Rastro de la reproducción en el registro de la sesión (logging.h), para encontrar por
// qué a veces un clip no aparece o la imagen se congela: apunta cuándo un clip que se ve
// no tiene fotograma o lleva un rato con el mismo, y cuándo el audio se queda sin
// muestras. Una línea al empezar cada suceso y otra al terminar (el suceso sigue abierto
// hasta que el clip se pone al día, aunque entre medias llegue algún fotograma suelto) y
// un resumen al pausar. Solo mira: no cambia cómo se reproduce.
class PlaybackTrace {
public:
    // Un clip que se ve en este paso de la reproducción.
    struct Visible {
        std::size_t track = 0;
        quint64 clip = 0;
        qint64 wanted = 0;     // fotograma del medio que toca
        qint64 shown = -1;     // el que se muestra (-1 = ninguno)
        bool finished = false; // su hilo llegó al final del medio (nada más que esperar)
    };
    // Texto con el estado del clip y su hilo, para la línea que abre un suceso.
    using Describe = std::function<QString(const Visible&)>;

    // Empieza a reproducir en `frame`. Los totales vuelven a cero. `timecode` escribe un
    // fotograma de la secuencia para el registro.
    void begin(qint64 frame, std::function<QString(qint64)> timecode);
    // Salto durante la reproducción: se cierran los sucesos abiertos.
    void restart(qint64 frame);
    // Un paso de la reproducción, ya en el fotograma `frame` de la secuencia.
    void step(qint64 frame, const std::vector<Visible>& visible, const Describe& describe);
    // Tramas de audio que faltaron desde el paso anterior (48 kHz).
    void audioMissing(qint64 frames);
    // Al pausar: cierra los sucesos y escribe el resumen.
    void end();

    bool active() const { return m_active; }

private:
    enum class Problem { None, Missing, Frozen };
    struct ClipState {
        qint64 shown = -2;       // último fotograma visto (-2 = aún nada)
        qint64 changedMs = 0;    // cuándo cambió por última vez
        qint64 changedFrame = 0; // y en qué fotograma de la secuencia
        qint64 firstSeenMs = 0;  // cuándo empezó a verse
        qint64 firstSeenFrame = 0;
        Problem open = Problem::None; // suceso abierto
        qint64 openedMs = 0;
        qint64 openedFrame = 0;
        qint64 lateFrames = 0; // fotogramas de la secuencia que duró
    };
    using Key = std::pair<std::size_t, quint64>;

    void close(const Key& key, ClipState& state, const char* how);

    std::function<QString(qint64)> m_timecode;
    bool m_active = false;
    QElapsedTimer m_clock;
    qint64 m_graceUntilMs = 0;
    qint64 m_firstFrame = 0;
    qint64 m_lastFrame = 0;
    std::map<Key, ClipState> m_clips;
    // Totales de esta reproducción.
    qint64 m_played = 0; // fotogramas que avanzó
    qint64 m_lateFrames = 0;
    int m_events = 0;
    qint64 m_audioMissingTotal = 0;
    qint64 m_audioBurst = 0; // tramas de audio que faltan en la racha actual
    qint64 m_audioBurstFrame = 0;
    int m_audioBursts = 0;
};
