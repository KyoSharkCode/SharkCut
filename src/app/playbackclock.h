#pragma once

#include <QElapsedTimer>
#include <QString>
#include <QtGlobal>

#include <memory>

namespace sharkcut {
class AudioOutput;
class AudioWorker;
} // namespace sharkcut

// Reloj de reproducción en microsegundos de la línea de tiempo.
//
// Si hay audio, manda el audio: la posición es la muestra que suena ahora mismo
// (sharkcut::AudioOutput::clock). Sin audio, o cuando el audio se acaba antes que el
// video, avanza con un temporizador desde la última posición conocida.
class PlaybackClock {
public:
    // Fuente de audio opcional (ambos punteros deben sobrevivir al reloj, o
    // reiniciarse con nullptr antes de destruirse).
    void setAudio(sharkcut::AudioOutput* output, sharkcut::AudioWorker* worker);

    // Empieza a contar desde `anchorUs` (y pide al audio que suene desde ahí).
    void start(qint64 anchorUs);

    // Detiene el audio; la posición deja de avanzar.
    void stop();

    // Posición actual. Mientras el audio se prepara, se queda en el punto de partida.
    qint64 nowUs();

private:
    sharkcut::AudioOutput* m_output = nullptr;
    sharkcut::AudioWorker* m_worker = nullptr;
    QElapsedTimer m_timer;
    qint64 m_anchorUs = 0;
    bool m_running = false;
    bool m_audioClock = false;
    quint64 m_audioRequest = 0;
};

// Abre la salida de audio predeterminada, o la muda si SHARKCUT_AUDIO_DEVICE=null
// (pruebas en equipos sin tarjeta de sonido). Devuelve nullptr si no hay salida.
// Si `description` no es nulo, recibe un texto para la interfaz.
std::unique_ptr<sharkcut::AudioOutput> createAudioOutput(QString* description);
