#pragma once

// Historial de deshacer/rehacer.
//
// Toda modificación pasa por apply(): la edición trabaja sobre una copia y, si cambia
// algo, esa copia pasa a ser el estado actual. Cada estado se guarda entero (compartido
// e inmutable); un proyecto ocupa poco (ids, posiciones y rutas), así que deshacer y
// rehacer son exactos e instantáneos.
//
// El editor guarda así cada proyecto abierto (Document, con todas sus secuencias); los
// tests del núcleo lo usan también con una sola secuencia (Project).

#include "core/project.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sharkcut {

template <typename State> class BasicHistory {
public:
    static constexpr std::size_t kMaxUndo = 500; // pasos que se conservan

    explicit BasicHistory(State state = {}) { reset(std::move(state)); }

    const State& state() const { return *m_states[m_index].state; }

    // Aplica `edit` a una copia. Si devuelve false o no cambia nada, no se registra.
    // `label` describe la edición ("Dividir", "Mover clip"…).
    bool apply(std::string label, const std::function<bool(State&)>& edit) {
        State copy = state();
        if (!edit(copy) || copy == state()) {
            return false;
        }
        m_states.resize(m_index + 1); // lo que se podía rehacer se descarta
        m_states.push_back({std::make_shared<const State>(std::move(copy)), std::move(label)});
        if (m_states.size() > kMaxUndo + 1) {
            m_states.erase(m_states.begin());
        }
        m_index = m_states.size() - 1;
        return true;
    }

    bool canUndo() const { return m_index > 0; }
    bool canRedo() const { return m_index + 1 < m_states.size(); }
    // Nombre de la edición que se desharía o reharía ("" si no hay).
    std::string undoLabel() const { return canUndo() ? m_states[m_index].label : std::string(); }
    std::string redoLabel() const {
        return canRedo() ? m_states[m_index + 1].label : std::string();
    }
    bool undo() {
        if (!canUndo()) {
            return false;
        }
        --m_index;
        return true;
    }
    bool redo() {
        if (!canRedo()) {
            return false;
        }
        ++m_index;
        return true;
    }

    // Estado actual, compartido e inmutable (p. ej. para guardarlo en otro hilo).
    std::shared_ptr<const State> snapshot() const { return m_states[m_index].state; }

    // Cambios sin guardar respecto al último estado guardado (o al inicial).
    bool modified() const { return m_states[m_index].state != m_saved; }
    void markSaved() { m_saved = m_states[m_index].state; }
    // Marca como guardado un estado concreto: el que se escribió, aunque mientras
    // tanto se haya seguido editando.
    void markSaved(std::shared_ptr<const State> state) { m_saved = std::move(state); }

    // Empieza de cero con otro estado (nuevo o recién abierto), sin historial.
    void reset(State state) {
        m_states.clear();
        m_states.push_back({std::make_shared<const State>(std::move(state)), {}});
        m_index = 0;
        m_saved = m_states.front().state;
    }

private:
    struct Entry {
        std::shared_ptr<const State> state;
        std::string label; // edición que llevó a este estado
    };

    std::vector<Entry> m_states;
    std::size_t m_index = 0;
    std::shared_ptr<const State> m_saved;
};

// Historial de una sola secuencia (lo usan los tests del núcleo).
class History : public BasicHistory<Project> {
public:
    using BasicHistory<Project>::BasicHistory;
    const Project& project() const { return state(); }
};

} // namespace sharkcut
