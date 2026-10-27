#pragma once

// Documento de SharkCut: un proyecto con su biblioteca de medios (con carpetas) y una o
// varias secuencias. Cada secuencia tiene su lienzo, sus fps y sus pistas; los medios son
// del proyecto y los comparten todas.
//
// Las operaciones de edición (edit.h) trabajan sobre un Project, que es la vista de una
// secuencia: sus ajustes y pistas más la biblioteca y el contador de ids del documento.
// edit() saca esa vista, aplica la operación y guarda el resultado en el documento.

#include "core/history.h"
#include "core/project.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sharkcut {

struct Folder {
    ObjectId id = 0;
    std::string name;
    // La de los recursos de SharkCut (efectos de sonido que se añaden solos). Se reconoce
    // por esta marca, no por su nombre; si se le cambia el nombre la pierde.
    bool resources = false;

    friend bool operator==(const Folder&, const Folder&) = default;
};

struct Sequence {
    ObjectId id = 0;
    std::string name;
    ObjectId folder = 0; // carpeta de la biblioteca (0 = raíz)
    ProjectSettings settings;
    std::vector<Track> tracks{Track{TrackKind::Video, {}}, Track{TrackKind::Audio, {}}};
    std::vector<Marker> markers{};
    std::vector<CaptionTrack> captionTracks{};

    friend bool operator==(const Sequence&, const Sequence&) = default;
};

struct Document {
    std::vector<MediaItem> media;
    std::vector<Folder> folders;
    std::vector<Sequence> sequences; // siempre al menos una
    ObjectId nextId = 1;

    const Sequence* sequence(ObjectId id) const;
    const Folder* folder(ObjectId id) const;

    // Vista de una secuencia para las operaciones de edición (vacía si no existe).
    Project view(ObjectId sequence) const;
    // Guarda en el documento lo que cambió en la vista de una secuencia.
    void store(ObjectId sequence, const Project& view);

    friend bool operator==(const Document&, const Document&) = default;
};

using DocumentHistory = BasicHistory<Document>;

// Pistas de una secuencia nueva: V1 y V2 encima, A1 y A2 debajo (gameplay, facecam,
// audio y música).
std::vector<Track> defaultTracks();

// Documento con una sola secuencia a partir de un proyecto de una secuencia (los
// archivos .sharkp anteriores a la versión 4, los tests…).
Document documentFromProject(Project project, std::string sequenceName);

// Aplica `change` a la vista de la secuencia y guarda el resultado. Devuelve lo que
// devuelva `change` (false si la secuencia no existe).
bool edit(Document& document, ObjectId sequence, const std::function<bool(Project&)>& change);

// --- Secuencias ---
ObjectId addSequence(Document& document, std::string name, ProjectSettings settings,
                     ObjectId folder = 0);
bool renameSequence(Document& document, ObjectId sequence, std::string name);
// Siempre queda al menos una secuencia.
bool removeSequence(Document& document, ObjectId sequence);
// Copia con ids nuevos (clips, enlaces, transiciones y sus sonidos). Con `canvas`, la
// copia usa ese lienzo (p. ej. 1080×1920 para "Duplicar como vertical"); los clips se
// quedan como estaban y se reencuadran después.
ObjectId duplicateSequence(Document& document, ObjectId sequence, std::string name,
                           std::optional<std::pair<int, int>> canvas = std::nullopt);

// --- Carpetas ---
ObjectId addFolder(Document& document, std::string name);
bool renameFolder(Document& document, ObjectId folder, std::string name);
// Lo que contenía pasa a la raíz.
bool removeFolder(Document& document, ObjectId folder);
// Mueve medios y secuencias a la carpeta (0 = raíz).
bool moveToFolder(Document& document, const std::vector<ObjectId>& items, ObjectId folder);
// La carpeta de los recursos de SharkCut; si no hay ninguna, la crea con `name`.
ObjectId resourceFolder(Document& document, std::string name);

// --- Medios ---
// Quita el medio de la biblioteca y sus clips de todas las secuencias.
bool removeMediaEverywhere(Document& document, ObjectId media);

} // namespace sharkcut
