#include "timelinemodel.h"

#include "editor.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace {
constexpr int kClipRole = Qt::UserRole + 1;
// Por debajo de este ancho (píxeles) un clip se agrupa con sus vecinos.
constexpr double kMinClipPixels = 16.0;
// Si cambian más filas que esto, es más rápido rehacer el modelo que avisar de cada una.
constexpr std::size_t kMaxIndividualChanges = 64;
} // namespace

TimelineClipsModel::TimelineClipsModel(QObject* parent) : QAbstractListModel(parent) {}

void TimelineClipsModel::setEditor(Editor* editor) {
    if (m_editor == editor) {
        return;
    }
    if (m_editor) {
        disconnect(m_editor, nullptr, this, nullptr);
    }
    m_editor = editor;
    if (m_editor) {
        // Tras una edición, enseguida: las filas deben corresponder ya al proyecto nuevo.
        connect(m_editor, &Editor::projectChanged, this, [this] { refresh(true); });
    }
    emit editorChanged();
    refresh(true);
}

void TimelineClipsModel::setFirstFrame(double frame) {
    if (frame != m_first) {
        m_first = frame;
        emit rangeChanged();
        scheduleRefresh(false);
    }
}

void TimelineClipsModel::setLastFrame(double frame) {
    if (frame != m_last) {
        m_last = frame;
        emit rangeChanged();
        scheduleRefresh(false);
    }
}

void TimelineClipsModel::scheduleRefresh(bool force) {
    m_forcePending = m_forcePending || force;
    if (m_refreshPending) {
        return;
    }
    m_refreshPending = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            const bool forced = m_forcePending;
            m_refreshPending = false;
            m_forcePending = false;
            refresh(forced);
        },
        Qt::QueuedConnection);
}

void TimelineClipsModel::setPixelsPerFrame(double scale) {
    if (scale > 0.0 && scale != m_scale) {
        m_scale = scale;
        emit rangeChanged();
        scheduleRefresh(true); // al cambiar el zoom cambia qué se agrupa
    }
}

int TimelineClipsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant TimelineClipsModel::data(const QModelIndex& index, int role) const {
    if (role != kClipRole || !index.isValid() || index.row() >= static_cast<int>(m_rows.size())) {
        return {};
    }
    return m_rows[static_cast<std::size_t>(index.row())].clip;
}

QHash<int, QByteArray> TimelineClipsModel::roleNames() const {
    return {{kClipRole, "datosClip"}};
}

void TimelineClipsModel::refresh(bool force) {
    const auto first = static_cast<std::int64_t>(std::floor(std::max(0.0, m_first)));
    const auto last = static_cast<std::int64_t>(std::ceil(std::max(m_first, m_last)));
    if (!force && first >= m_windowStart && last <= m_windowEnd) {
        return; // lo visible sigue dentro de lo cargado
    }
    // Media pantalla de margen a cada lado: desplazarse un poco no cambia nada.
    const std::int64_t margin = std::max<std::int64_t>((last - first) / 2, 1);
    m_windowStart = std::max<std::int64_t>(0, first - margin);
    m_windowEnd = last + margin;

    // Clips que tocan la ventana (cada pista está ordenada y sin solapes: búsqueda binaria).
    std::vector<Row> fresh;
    if (m_editor) {
        const sharkcut::Project& p = m_editor->project();
        for (std::size_t t = 0; t < p.tracks.size(); ++t) {
            const auto& clips = p.tracks[t].clips;
            auto it = std::upper_bound(
                clips.begin(), clips.end(), m_windowStart,
                [](std::int64_t frame, const sharkcut::Clip& c) { return frame < c.end(); });
            const bool video = p.tracks[t].kind == sharkcut::TrackKind::Video;
            // Bloque de clips diminutos en curso (se cierra al llegar a uno visible o a
            // un hueco que se vería).
            struct Block {
                qint64 id = 0;
                std::int64_t start = 0;
                std::int64_t end = 0;
                int count = 0;
            } block;
            auto closeBlock = [&] {
                if (block.count == 0) {
                    return;
                }
                QVariantMap entry{
                    {QStringLiteral("id"), block.id},
                    {QStringLiteral("block"), true},
                    {QStringLiteral("count"), block.count},
                    {QStringLiteral("track"), static_cast<int>(t)},
                    {QStringLiteral("video"), video},
                    {QStringLiteral("start"), static_cast<qint64>(block.start)},
                    {QStringLiteral("length"), static_cast<qint64>(block.end - block.start)},
                    {QStringLiteral("link"), qint64{0}},
                    {QStringLiteral("in"), qint64{0}},
                    {QStringLiteral("media"), qint64{0}},
                    {QStringLiteral("volumeDb"), 0.0},
                    {QStringLiteral("transformed"), false},
                    {QStringLiteral("name"), QString()},
                };
                fresh.push_back({block.id, std::move(entry)});
                block = {};
            };
            for (; it != clips.end() && it->start <= m_windowEnd; ++it) {
                const bool tiny = static_cast<double>(it->length) * m_scale < kMinClipPixels;
                const bool joins =
                    block.count > 0 &&
                    static_cast<double>(it->start - block.end) * m_scale < kMinClipPixels;
                if (tiny && (block.count == 0 || joins)) {
                    if (block.count == 0) {
                        // Id negativo y estable (el del primer clip): no choca con los clips.
                        block.id = -static_cast<qint64>(it->id);
                        block.start = it->start;
                    }
                    block.end = it->end();
                    ++block.count;
                    continue;
                }
                closeBlock();
                if (tiny) { // empieza otro bloque tras un hueco
                    block.id = -static_cast<qint64>(it->id);
                    block.start = it->start;
                    block.end = it->end();
                    block.count = 1;
                    continue;
                }
                fresh.push_back({static_cast<qint64>(it->id), m_editor->clipEntry(t, *it)});
            }
            closeBlock();
        }
    }

    std::unordered_map<qint64, std::size_t> wanted;
    for (std::size_t i = 0; i < fresh.size(); ++i) {
        wanted[fresh[i].id] = i;
    }
    // Si cambia casi todo (otra secuencia, un salto lejano), se rehace de una vez.
    std::size_t kept = 0;
    for (const Row& row : m_rows) {
        kept += wanted.count(row.id);
    }
    std::size_t changed = m_rows.size() - kept + (fresh.size() - kept);
    for (const Row& row : m_rows) {
        const auto it = wanted.find(row.id);
        if (it != wanted.end() && fresh[it->second].clip != row.clip) {
            ++changed;
        }
    }
    if (kept * 2 < m_rows.size() || changed > kMaxIndividualChanges) {
        beginResetModel();
        m_rows = std::move(fresh);
        endResetModel();
        return;
    }
    // Quitar las que ya no están (de atrás adelante).
    for (std::size_t i = m_rows.size(); i-- > 0;) {
        if (!wanted.count(m_rows[i].id)) {
            beginRemoveRows({}, static_cast<int>(i), static_cast<int>(i));
            m_rows.erase(m_rows.begin() + static_cast<std::ptrdiff_t>(i));
            endRemoveRows();
        }
    }
    // Actualizar las que siguen (solo si cambiaron) y añadir las nuevas al final.
    std::unordered_set<qint64> present;
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        Row& row = m_rows[i];
        present.insert(row.id);
        const QVariantMap& now = fresh[wanted[row.id]].clip;
        if (row.clip != now) {
            row.clip = now;
            const QModelIndex index = createIndex(static_cast<int>(i), 0);
            emit dataChanged(index, index, {kClipRole});
        }
    }
    std::vector<Row> added;
    for (Row& row : fresh) {
        if (!present.count(row.id)) {
            added.push_back(std::move(row));
        }
    }
    if (!added.empty()) {
        const auto start = static_cast<int>(m_rows.size());
        beginInsertRows({}, start, start + static_cast<int>(added.size()) - 1);
        std::move(added.begin(), added.end(), std::back_inserter(m_rows));
        endInsertRows();
    }
}
