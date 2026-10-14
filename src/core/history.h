#pragma once

// Historial de deshacer/rehacer del proyecto.
//
// Toda modificación del proyecto pasa por apply(): la edición trabaja sobre una copia
// y, si cambia algo, esa copia pasa a ser el estado actual. Cada estado se guarda
// entero (compartido e inmutable); un proyecto ocupa poco (ids, posiciones y rutas),
// así que deshacer y rehacer son exactos e instantáneos.

#include "core/project.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sharkcut {

class History {
public:
    static constexpr std::size_t kMaxUndo = 500; // pasos que se conservan

    explicit History(Project project = {});

    const Project& project() const { return *m_states[m_index].project; }

    // Aplica `edit` a una copia del proyecto. Si devuelve false o no cambia nada, no
    // se registra. `label` describe la edición ("Dividir", "Mover clip"…).
    bool apply(std::string label, const std::function<bool(Project&)>& edit);

    bool canUndo() const { return m_index > 0; }
    bool canRedo() const { return m_index + 1 < m_states.size(); }
    // Nombre de la edición que se desharía o reharía ("" si no hay).
    std::string undoLabel() const;
    std::string redoLabel() const;
    bool undo();
    bool redo();

    // Cambios sin guardar respecto al último markSaved() (o al proyecto inicial).
    bool modified() const { return m_states[m_index].project != m_saved; }
    void markSaved() { m_saved = m_states[m_index].project; }

    // Empieza de cero con otro proyecto (nuevo o recién abierto), sin historial.
    void reset(Project project);

private:
    struct State {
        std::shared_ptr<const Project> project;
        std::string label; // edición que llevó a este estado
    };

    std::vector<State> m_states;
    std::size_t m_index = 0;
    std::shared_ptr<const Project> m_saved;
};

} // namespace sharkcut
