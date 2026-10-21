#pragma once

#include <QString>

// Registro de la sesión: todo lo que la app, Qt, FFmpeg y whisper escriben (stderr y los
// mensajes de Qt) va a "<datos de la app>/registros/sharkcut-<fecha>.log". Se guardan
// las últimas sesiones. En Windows, si la app se cierra por un fallo, se apunta el
// código del error y se guarda un volcado (ultimo-cierre.dmp) para depurarlo.
//
// Con la variable de entorno SHARKCUT_LOG=consola todo sigue saliendo por la consola.
namespace logging {

// Llamar al principio de main(), tras crear la aplicación (necesita su nombre).
void install();
// Apunta que la app se cerró con normalidad (si falta, es que se cerró de golpe).
void closing();
QString folder();

} // namespace logging
