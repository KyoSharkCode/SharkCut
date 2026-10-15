#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace sharkcut {

// Proxies: copias ligeras de los videos para editar con fluidez (decodificar, buscar e ir
// hacia atrás cuesta mucho menos). Se usan solo en la vista previa; la exportación
// siempre lee los originales. Van en la caché del proyecto, nunca junto a los medios.
//
// Un proxy es un MP4 H.264 sin audio, a kProxyHeight de alto (o menos, si el original
// ya es pequeño), con un fotograma clave cada kProxyGop fotogramas y los mismos fps e
// índices de fotograma que el original (también si al original le faltan fotogramas):
// el fotograma N del proxy es el N del original.

inline constexpr int kProxyHeight = 540;
inline constexpr int kProxyGop = 15;

// Escribe el proxy de `source` en `target` (rutas UTF-8). Se escribe en un temporal que
// se renombra al terminar: si existe `target`, está completo. `progress` recibe
// (fotogramas hechos, total); `cancel` lo detiene (sin dejar archivo). Lanza MediaError.
void writeProxy(const std::string& source, const std::string& target,
                const std::function<void(std::int64_t, std::int64_t)>& progress = {},
                const std::atomic<bool>* cancel = nullptr);

// Nombre del proxy de un medio en la carpeta de proxies: cambia si el original cambia
// (ruta, tamaño o fecha). Vacío si el archivo no existe.
std::string proxyFileName(const std::string& mediaPath);

} // namespace sharkcut
