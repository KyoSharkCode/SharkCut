#include "core/history.h"

namespace sharkcut {

History::History(Project project) {
    reset(std::move(project));
}

void History::reset(Project project) {
    m_states.clear();
    m_states.push_back({std::make_shared<const Project>(std::move(project)), {}});
    m_index = 0;
    m_saved = m_states.front().project;
}

bool History::apply(std::string label, const std::function<bool(Project&)>& edit) {
    Project copy = project();
    if (!edit(copy) || copy == project()) {
        return false;
    }
    m_states.resize(m_index + 1); // lo que se podía rehacer se descarta
    m_states.push_back({std::make_shared<const Project>(std::move(copy)), std::move(label)});
    if (m_states.size() > kMaxUndo + 1) {
        m_states.erase(m_states.begin());
    }
    m_index = m_states.size() - 1;
    return true;
}

std::string History::undoLabel() const {
    return canUndo() ? m_states[m_index].label : std::string();
}

std::string History::redoLabel() const {
    return canRedo() ? m_states[m_index + 1].label : std::string();
}

bool History::undo() {
    if (!canUndo()) {
        return false;
    }
    --m_index;
    return true;
}

bool History::redo() {
    if (!canRedo()) {
        return false;
    }
    ++m_index;
    return true;
}

} // namespace sharkcut
