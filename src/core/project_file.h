#pragma once

// Archivo de proyecto .sharkp: JSON con un campo `version`.
//
// Versión 2: pistas en número variable (con ocultar, silenciar y bloquear), enlaces
// entre clips guardados tal cual, y transformación (video) y volumen (audio) por clip.
// Versión 3: transiciones en los cortes de las pistas de video, con su sonido.
// Versión 4: varias secuencias por proyecto y carpetas en la biblioteca; también qué
// secuencias estaban abiertas (pestañas). Las versiones anteriores se abren como un
// proyecto con una sola secuencia, con el nombre del archivo.
//
// Las rutas de los medios se guardan absolutas y, además, relativas a la carpeta
// del proyecto: si se mueve la carpeta entera (proyecto + videos) a otro disco o
// equipo, al abrirlo se encuentran por la ruta relativa.
//
// Estas funciones acceden al disco: no llamarlas desde el hilo de la interfaz.

#include "core/document.h"
#include "core/project.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace sharkcut {

// 5: pantalla verde por clip (transform.chromaKey). 6: pistas de subtítulos
// (captionTracks de cada secuencia). 7: keyframes de los clips (animation). Abre también
// la 1 a la 6.
inline constexpr int kProjectFileVersion = 7;
inline constexpr std::string_view kProjectExtension = ".sharkp";

class ProjectError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Rutas en UTF-8 ↔ std::filesystem::path (en Windows, std::string no es UTF-8).
std::filesystem::path pathFromUtf8(std::string_view utf8);
std::string utf8FromPath(const std::filesystem::path& path);

// Pestañas de secuencias abiertas al guardar (no es parte del deshacer).
struct Workspace {
    std::vector<ObjectId> openSequences;
    ObjectId activeSequence = 0;
};

struct LoadedDocument {
    Document document;
    Workspace workspace; // siempre con al menos una secuencia abierta y la activa entre ellas
};

// Documento completo (todas las secuencias). `projectFile` sirve para las rutas relativas.
std::string documentToJson(const Document& document, const std::filesystem::path& projectFile,
                           const Workspace& workspace);
LoadedDocument documentFromJson(std::string_view json, const std::filesystem::path& projectFile);
void saveDocument(const Document& document, const Workspace& workspace,
                  const std::filesystem::path& file);
LoadedDocument loadDocument(const std::filesystem::path& file);

// Proyecto de una sola secuencia (lo usan los tests y las herramientas): se guarda como
// un documento con esa secuencia y, al leer, se obtiene la secuencia activa.
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
