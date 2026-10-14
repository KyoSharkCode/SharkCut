#pragma once

// Archivo de proyecto .sharkp: JSON con un campo `version`.
//
// Versión 2: pistas en número variable (con ocultar, silenciar y bloquear), enlaces
// entre clips guardados tal cual, y transformación (video) y volumen (audio) por clip.
// Los archivos de la versión 1 (una pista de video y una de audio) se siguen abriendo.
//
// Las rutas de los medios se guardan absolutas y, además, relativas a la carpeta
// del proyecto: si se mueve la carpeta entera (proyecto + videos) a otro disco o
// equipo, al abrirlo se encuentran por la ruta relativa.
//
// Estas funciones acceden al disco: no llamarlas desde el hilo de la interfaz.

#include "core/project.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace sharkcut {

inline constexpr int kProjectFileVersion = 2;
inline constexpr std::string_view kProjectExtension = ".sharkp";

class ProjectError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Rutas en UTF-8 ↔ std::filesystem::path (en Windows, std::string no es UTF-8).
std::filesystem::path pathFromUtf8(std::string_view utf8);
std::string utf8FromPath(const std::filesystem::path& path);

// Serializa el proyecto. `projectFile` (donde se guardará) sirve para las rutas
// relativas; puede estar vacío.
std::string projectToJson(const Project& project, const std::filesystem::path& projectFile);

// Lee un proyecto. Si un medio no está en su ruta absoluta pero sí en la relativa a
// `projectFile`, se usa esa. Lanza ProjectError si el archivo no es válido o es de
// una versión más nueva de SharkCut.
Project projectFromJson(std::string_view json, const std::filesystem::path& projectFile);

// Guarda en disco sin dejar nunca un archivo a medias: escribe un temporal y lo
// renombra. Lanza ProjectError si falla.
void saveProject(const Project& project, const std::filesystem::path& file);

// Lanza ProjectError si no se puede leer o no es válido.
Project loadProject(const std::filesystem::path& file);

} // namespace sharkcut
