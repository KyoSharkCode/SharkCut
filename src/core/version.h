#pragma once

// Números de versión ("0.1.0", "v0.2.1") para buscar actualizaciones.

#include <array>
#include <optional>
#include <string_view>

namespace sharkcut {

using Version = std::array<int, 3>; // mayor, menor, parche

// "v1.2.3", "1.2.3" o "1.2" (parche 0); con un sufijo ("-beta") se ignora. nullopt si no
// empieza por un número de versión.
std::optional<Version> parseVersion(std::string_view text);

// ¿`latest` es posterior a `current`? (si alguna no se entiende, no).
bool isNewerVersion(std::string_view latest, std::string_view current);

} // namespace sharkcut
