#pragma once

#include <stdexcept>
#include <string>

namespace sharkcut {

// Error al abrir, analizar o decodificar un archivo de medios.
class MediaError : public std::runtime_error {
public:
    explicit MediaError(const std::string& message) : std::runtime_error(message) {}
};

} // namespace sharkcut
