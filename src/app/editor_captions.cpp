// Subtítulos en el editor: pistas, selección, edición, estilos y ajustes guardados, y
// archivos .srt. (La transcripción automática va aparte.)

#include "editor.h"

#include "core/captions.h"
#include "core/edit.h"

#include <QColor>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>
#include <QSettings>
#include <QThreadPool>

#include <algorithm>
#include <limits>

using sharkcut::Caption;
using sharkcut::CaptionStyle;
using sharkcut::ObjectId;
using sharkcut::Project;

namespace {

QString hex(std::uint32_t color) {
    return QStringLiteral("#%1").arg(color & 0xFFFFFFu, 6, 16, QLatin1Char('0')).toUpper();
}

QVariantMap styleToMap(const CaptionStyle& s) {
    return {
        {QStringLiteral("font"), QString::fromStdString(s.font)},
        {QStringLiteral("size"), s.size},
        {QStringLiteral("bold"), s.bold},
        {QStringLiteral("italic"), s.italic},
        {QStringLiteral("uppercase"), s.uppercase},
        {QStringLiteral("color"), hex(s.color)},
        {QStringLiteral("outlineColor"), hex(s.outlineColor)},
        {QStringLiteral("outline"), s.outline},
        {QStringLiteral("shadowColor"), hex(s.shadowColor)},
        {QStringLiteral("shadow"), s.shadow},
        {QStringLiteral("boxColor"), hex(s.boxColor)},
        {QStringLiteral("box"), s.box},
        {QStringLiteral("x"), s.x},
        {QStringLiteral("y"), s.y},
        {QStringLiteral("maxWidth"), s.maxWidth},
    };
}

// `base` con las claves de `values` que traiga.
CaptionStyle styleWith(CaptionStyle s, const QVariantMap& values) {
    const auto number = [&values](const char* key, double& member) {
        const auto it = values.find(QLatin1String(key));
        if (it != values.end()) {
            member = it->toDouble();
        }
    };
    const auto flag = [&values](const char* key, bool& member) {
        const auto it = values.find(QLatin1String(key));
        if (it != values.end()) {
            member = it->toBool();
        }
    };
    const auto color = [&values](const char* key, std::uint32_t& member) {
        const auto it = values.find(QLatin1String(key));
        if (it != values.end()) {
            const QColor c(it->toString());
            if (c.isValid()) {
                member = static_cast<std::uint32_t>(c.rgb()) & 0xFFFFFFu;
            }
        }
    };
    if (const auto it = values.find(QStringLiteral("font")); it != values.end()) {
        s.font = it->toString().toStdString();
    }
    number("size", s.size);
    flag("bold", s.bold);
    flag("italic", s.italic);
    flag("uppercase", s.uppercase);
    color("color", s.color);
    color("outlineColor", s.outlineColor);
    number("outline", s.outline);
    color("shadowColor", s.shadowColor);
    number("shadow", s.shadow);
    color("boxColor", s.boxColor);
    number("box", s.box);
    number("x", s.x);
    number("y", s.y);
    number("maxWidth", s.maxWidth);
    return sharkcut::clampCaptionStyle(s);
}

// Ajustes de fábrica: neutros (sin marca), para empezar.
std::vector<std::pair<QString, CaptionStyle>> builtinPresets() {
    std::vector<std::pair<QString, CaptionStyle>> list;
    CaptionStyle classic; // los valores por defecto
    list.emplace_back(QStringLiteral("Clásico"), classic);

    CaptionStyle shorts;
    shorts.size = 84;
    shorts.uppercase = true;
    shorts.color = 0xFFD800;
    shorts.outline = 9;
    shorts.y = 0.62;
    shorts.maxWidth = 0.8;
    list.emplace_back(QStringLiteral("Shorts amarillo"), shorts);

    CaptionStyle big;
    big.size = 96;
    big.uppercase = true;
    big.outline = 10;
    big.shadow = 0.5;
    big.y = 0.5;
    big.maxWidth = 0.8;
    list.emplace_back(QStringLiteral("Grande centrado"), big);

    CaptionStyle box;
    box.size = 54;
    box.bold = false;
    box.outline = 0;
    box.box = 0.65;
    box.y = 0.86;
    list.emplace_back(QStringLiteral("Caja negra"), box);

    CaptionStyle thin;
    thin.size = 60;
    thin.outline = 3;
    thin.shadow = 0.7;
    list.emplace_back(QStringLiteral("Contorno fino con sombra"), thin);
    return list;
}

constexpr auto kPresetsKey = "subtitulos/ajustes";

// Ajustes guardados por Kyo: [{name, style}] en QSettings.
std::vector<std::pair<QString, CaptionStyle>> userPresets() {
    std::vector<std::pair<QString, CaptionStyle>> list;
    const QJsonArray array =
        QJsonDocument::fromJson(QSettings().value(QLatin1String(kPresetsKey)).toByteArray())
            .array();
    for (const QJsonValue& v : array) {
        const QJsonObject o = v.toObject();
        const QString name = o.value(QStringLiteral("name")).toString();
        if (!name.isEmpty()) {
            list.emplace_back(
                name, styleWith({}, o.value(QStringLiteral("style")).toObject().toVariantMap()));
        }
    }
    return list;
}

void storeUserPresets(const std::vector<std::pair<QString, CaptionStyle>>& list) {
    QJsonArray array;
    for (const auto& [name, style] : list) {
        array.append(
            QJsonObject{{QStringLiteral("name"), name},
                        {QStringLiteral("style"), QJsonObject::fromVariantMap(styleToMap(style))}});
    }
    QSettings().setValue(QLatin1String(kPresetsKey),
                         QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QString localPath(const QUrl& url) {
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

} // namespace

// --- Lectura -------------------------------------------------------------------------

QVariantList Editor::captionTracks() const {
    QVariantList list;
    const Project& p = project();
    for (std::size_t t = 0; t < p.captionTracks.size(); ++t) {
        const auto& track = p.captionTracks[t];
        list.append(QVariantMap{
            {QStringLiteral("index"), static_cast<int>(t)},
            {QStringLiteral("name"), QStringLiteral("S%1").arg(t + 1)},
            {QStringLiteral("hidden"), track.hidden},
            {QStringLiteral("locked"), track.locked},
            {QStringLiteral("count"), static_cast<int>(track.captions.size())},
        });
    }
    return list;
}

QVariantList Editor::captionSelection() const {
    QVariantList list;
    for (const ObjectId id : m_selectedCaptions) {
        list.append(static_cast<qint64>(id));
    }
    return list;
}

QVariantMap Editor::selectedCaption() const {
    const Project& p = project();
    if (m_selectedCaptions.empty()) {
        return {{QStringLiteral("count"), 0}};
    }
    const auto ref = p.findCaption(m_selectedCaptions.front());
    if (!ref) {
        return {{QStringLiteral("count"), 0}};
    }
    const auto& track = p.captionTracks[ref->track];
    const Caption& c = track.captions[ref->index];
    CaptionStyle shown = p.styleOf(ref->track, c);
    if (m_captionPreview) {
        shown = m_captionPreview->style;
    }
    return {
        {QStringLiteral("count"), static_cast<int>(m_selectedCaptions.size())},
        {QStringLiteral("id"), static_cast<qint64>(c.id)},
        {QStringLiteral("track"), static_cast<int>(ref->track)},
        {QStringLiteral("trackName"), QStringLiteral("S%1").arg(ref->track + 1)},
        {QStringLiteral("locked"), track.locked},
        {QStringLiteral("start"), static_cast<qint64>(c.start)},
        {QStringLiteral("end"), static_cast<qint64>(c.end())},
        {QStringLiteral("text"), QString::fromStdString(c.text)},
        {QStringLiteral("words"), !c.words.empty()},
        {QStringLiteral("ownStyle"), c.style.has_value()},
        {QStringLiteral("style"), styleToMap(shown)},
        {QStringLiteral("trackStyle"), styleToMap(track.style)},
    };
}

QVariantList Editor::captionPresets() const {
    QVariantList list;
    for (const auto& [name, style] : builtinPresets()) {
        list.append(QVariantMap{{QStringLiteral("name"), name},
                                {QStringLiteral("builtin"), true},
                                {QStringLiteral("style"), styleToMap(style)}});
    }
    for (const auto& [name, style] : userPresets()) {
        list.append(QVariantMap{{QStringLiteral("name"), name},
                                {QStringLiteral("builtin"), false},
                                {QStringLiteral("style"), styleToMap(style)}});
    }
    return list;
}

// --- Pistas --------------------------------------------------------------------------

int Editor::addCaptionTrack() {
    int created = -1;
    edit(QStringLiteral("Añadir pista de subtítulos"), [&created](Project& p) {
        created = static_cast<int>(sharkcut::addCaptionTrack(p));
        return true;
    });
    return created;
}

void Editor::removeCaptionTrack(int track) {
    edit(QStringLiteral("Quitar pista de subtítulos"), [track](Project& p) {
        return track >= 0 && sharkcut::removeCaptionTrack(p, static_cast<std::size_t>(track));
    });
}

void Editor::setCaptionTrackHidden(int track, bool hidden) {
    edit(hidden ? QStringLiteral("Ocultar subtítulos") : QStringLiteral("Mostrar subtítulos"),
         [track, hidden](Project& p) {
             return track >= 0 &&
                    sharkcut::setCaptionTrackHidden(p, static_cast<std::size_t>(track), hidden);
         });
}

void Editor::setCaptionTrackLocked(int track, bool locked) {
    edit(locked ? QStringLiteral("Bloquear subtítulos") : QStringLiteral("Desbloquear subtítulos"),
         [track, locked](Project& p) {
             return track >= 0 &&
                    sharkcut::setCaptionTrackLocked(p, static_cast<std::size_t>(track), locked);
         });
}

// --- Subtítulos ----------------------------------------------------------------------

void Editor::addCaptionAtPlayhead(int track) {
    const std::int64_t frame = m_currentFrame;
    const std::int64_t length =
        std::max<std::int64_t>(1, project().settings.frameRate.nominal() * 3);
    ObjectId created = 0;
    const bool done = edit(QStringLiteral("Añadir subtítulo"), [&](Project& p) {
        std::size_t t = 0;
        if (p.captionTracks.empty()) {
            t = sharkcut::addCaptionTrack(p);
        } else if (track >= 0 && static_cast<std::size_t>(track) < p.captionTracks.size()) {
            t = static_cast<std::size_t>(track);
        }
        created = sharkcut::addCaption(p, t, frame, length, "Texto");
        return created != 0;
    });
    if (done) {
        m_selection.clear();
        m_selectedTransition = 0;
        m_selectedCaptions = {created};
        emit selectionChanged();
    } else {
        setError(QStringLiteral("Ya hay un subtítulo en el cabezal (o la pista está bloqueada)"));
    }
}

void Editor::selectCaption(qint64 id, bool add) {
    const auto caption = static_cast<ObjectId>(id);
    if (!project().caption(caption)) {
        return;
    }
    m_selection.clear();
    m_selectedTransition = 0;
    const auto it = std::find(m_selectedCaptions.begin(), m_selectedCaptions.end(), caption);
    if (!add) {
        m_selectedCaptions = {caption};
    } else if (it != m_selectedCaptions.end()) {
        m_selectedCaptions.erase(it);
    } else {
        m_selectedCaptions.push_back(caption);
    }
    m_captionPreview.reset();
    emit selectionChanged();
}

void Editor::pruneCaptionSelection() {
    std::erase_if(m_selectedCaptions,
                  [this](ObjectId id) { return project().caption(id) == nullptr; });
}

std::vector<std::size_t> Editor::selectedCaptionTracks() const {
    std::vector<std::size_t> tracks;
    for (const ObjectId id : m_selectedCaptions) {
        if (const auto ref = project().findCaption(id);
            ref && std::find(tracks.begin(), tracks.end(), ref->track) == tracks.end()) {
            tracks.push_back(ref->track);
        }
    }
    return tracks;
}

namespace {

// Los que se mueven juntos: la selección si `id` está en ella, o solo `id`.
std::vector<ObjectId> movingGroup(const std::vector<ObjectId>& selection, ObjectId id) {
    return std::find(selection.begin(), selection.end(), id) != selection.end()
               ? selection
               : std::vector<ObjectId>{id};
}

} // namespace

qint64 Editor::limitCaptionMove(qint64 id, qint64 delta) const {
    const Project& p = project();
    const std::vector<ObjectId> group = movingGroup(m_selectedCaptions, static_cast<ObjectId>(id));
    std::int64_t result = delta;
    for (const ObjectId cid : group) {
        const auto ref = p.findCaption(cid);
        if (!ref) {
            continue;
        }
        const auto& list = p.captionTracks[ref->track].captions;
        const Caption& c = list[ref->index];
        // Vecinos que no se mueven con el grupo.
        std::int64_t low = 0;
        std::int64_t high = std::numeric_limits<std::int64_t>::max();
        for (std::size_t i = ref->index; i-- > 0;) {
            if (std::find(group.begin(), group.end(), list[i].id) == group.end()) {
                low = list[i].end();
                break;
            }
        }
        for (std::size_t i = ref->index + 1; i < list.size(); ++i) {
            if (std::find(group.begin(), group.end(), list[i].id) == group.end()) {
                high = list[i].start;
                break;
            }
        }
        result = std::max(result, low - c.start);
        if (high != std::numeric_limits<std::int64_t>::max()) {
            result = std::min(result, high - c.end());
        }
    }
    return result;
}

void Editor::moveCaptions(qint64 id, qint64 delta) {
    delta = limitCaptionMove(id, delta);
    if (delta == 0) {
        return;
    }
    std::vector<ObjectId> group = movingGroup(m_selectedCaptions, static_cast<ObjectId>(id));
    edit(QStringLiteral("Mover subtítulos"), [&group, delta](Project& p) {
        // Primero los que van por delante, para que ninguno choque con otro del grupo.
        std::sort(group.begin(), group.end(), [&p, delta](ObjectId a, ObjectId b) {
            const Caption* ca = p.caption(a);
            const Caption* cb = p.caption(b);
            if (!ca || !cb) {
                return false;
            }
            return delta > 0 ? ca->start > cb->start : ca->start < cb->start;
        });
        bool changed = false;
        for (const ObjectId cid : group) {
            if (const Caption* c = p.caption(cid)) {
                changed = sharkcut::moveCaption(p, cid, c->start + delta) || changed;
            }
        }
        return changed;
    });
}

QVariantMap Editor::captionRoom(qint64 id) const {
    const Project& p = project();
    const auto ref = p.findCaption(static_cast<ObjectId>(id));
    if (!ref) {
        return {};
    }
    const auto [low, high] = sharkcut::captionEdgeRoom(p, static_cast<ObjectId>(id));
    const auto& list = p.captionTracks[ref->track].captions;
    const sharkcut::Caption& c = list[ref->index];
    const bool before = ref->index > 0 && list[ref->index - 1].end() == c.start;
    const bool after = ref->index + 1 < list.size() && list[ref->index + 1].start == c.end();
    return {
        {QStringLiteral("low"), static_cast<qint64>(low)},
        {QStringLiteral("high"),
         high == std::numeric_limits<std::int64_t>::max() ? qint64{-1} : static_cast<qint64>(high)},
        // Vecinos pegados (0 si no hay): su borde se mueve con el de este.
        {QStringLiteral("before"),
         before ? static_cast<qint64>(list[ref->index - 1].id) : qint64{0}},
        {QStringLiteral("after"), after ? static_cast<qint64>(list[ref->index + 1].id) : qint64{0}},
    };
}

void Editor::trimCaption(qint64 id, qint64 start, qint64 end) {
    edit(QStringLiteral("Recortar subtítulo"), [id, start, end](Project& p) {
        return sharkcut::trimCaption(p, static_cast<ObjectId>(id), start, end);
    });
}

void Editor::trimCaptionEdge(qint64 id, bool end, qint64 frame) {
    edit(QStringLiteral("Recortar subtítulo"), [id, end, frame](Project& p) {
        return sharkcut::trimCaptionEdge(p, static_cast<ObjectId>(id), end, frame);
    });
}

void Editor::splitCaption(qint64 id, qint64 frame) {
    edit(QStringLiteral("Dividir subtítulo"), [id, frame](Project& p) {
        return sharkcut::splitCaptionsAt(p, frame, {static_cast<ObjectId>(id)});
    });
}

void Editor::setCaptionText(qint64 id, const QString& text) {
    const std::string utf8 = text.toStdString();
    edit(QStringLiteral("Texto del subtítulo"), [id, &utf8](Project& p) {
        return sharkcut::setCaptionText(p, static_cast<ObjectId>(id), utf8);
    });
}

// --- Estilo --------------------------------------------------------------------------

std::optional<CaptionStyle> Editor::captionStyleWith(const QVariantMap& values,
                                                     bool wholeTrack) const {
    const Project& p = project();
    if (m_selectedCaptions.empty()) {
        return std::nullopt;
    }
    const auto ref = p.findCaption(m_selectedCaptions.front());
    if (!ref) {
        return std::nullopt;
    }
    const Caption& c = p.captionTracks[ref->track].captions[ref->index];
    // Mientras se arrastra, la base es lo que se previsualiza (así acaba donde se soltó).
    const CaptionStyle base = m_captionPreview ? m_captionPreview->style
                              : wholeTrack     ? p.captionTracks[ref->track].style
                                               : p.styleOf(ref->track, c);
    return styleWith(base, values);
}

void Editor::previewCaptionStyle(const QVariantMap& values, bool wholeTrack) {
    const auto style = captionStyleWith(values, wholeTrack);
    if (!style) {
        return;
    }
    CaptionPreview preview;
    preview.style = *style;
    if (wholeTrack) {
        preview.tracks = selectedCaptionTracks();
    } else {
        preview.captions = m_selectedCaptions;
    }
    m_captionPreview = std::move(preview);
    emit selectedClipChanged();
    emit compositionChanged();
}

void Editor::endCaptionPreview() {
    if (m_captionPreview) {
        m_captionPreview.reset();
        emit selectedClipChanged();
        emit compositionChanged();
    }
}

void Editor::setCaptionStyle(const QVariantMap& values, bool wholeTrack, const QString& label) {
    const auto style = captionStyleWith(values, wholeTrack);
    m_captionPreview.reset();
    if (!style) {
        return;
    }
    const std::vector<ObjectId> captions = m_selectedCaptions;
    const std::vector<std::size_t> tracks = selectedCaptionTracks();
    const bool changed =
        edit(label.isEmpty() ? QStringLiteral("Estilo de subtítulos") : label, [&](Project& p) {
            if (!wholeTrack) {
                return sharkcut::setCaptionStyle(p, captions, *style);
            }
            bool any = false;
            for (const std::size_t t : tracks) {
                any = sharkcut::setCaptionTrackStyle(p, t, *style, true) || any;
            }
            return any;
        });
    if (!changed) {
        emit selectedClipChanged();
        emit compositionChanged();
    }
}

void Editor::applyCaptionPreset(const QString& name, bool wholeTrack) {
    for (const QVariant& v : captionPresets()) {
        const QVariantMap preset = v.toMap();
        if (preset.value(QStringLiteral("name")).toString() == name) {
            m_captionPreview.reset();
            // El ajuste entero sustituye al estilo (no se mezcla con el anterior).
            const CaptionStyle style = styleWith({}, preset.value(QStringLiteral("style")).toMap());
            const std::vector<ObjectId> captions = m_selectedCaptions;
            const std::vector<std::size_t> tracks = selectedCaptionTracks();
            edit(QStringLiteral("Ajuste «%1»").arg(name), [&](Project& p) {
                if (!wholeTrack) {
                    return sharkcut::setCaptionStyle(p, captions, style);
                }
                bool any = false;
                for (const std::size_t t : tracks) {
                    any = sharkcut::setCaptionTrackStyle(p, t, style, true) || any;
                }
                return any;
            });
            return;
        }
    }
}

void Editor::resetCaptionStyle() {
    m_captionPreview.reset();
    const std::vector<ObjectId> captions = m_selectedCaptions;
    edit(QStringLiteral("Estilo de la pista"),
         [&captions](Project& p) { return sharkcut::setCaptionStyle(p, captions, std::nullopt); });
}

void Editor::saveCaptionPreset(const QString& name) {
    const QString trimmed = name.trimmed();
    const auto style = captionStyleWith({}, false);
    if (trimmed.isEmpty() || !style) {
        return;
    }
    for (const auto& [builtin, ignored] : builtinPresets()) {
        if (builtin.compare(trimmed, Qt::CaseInsensitive) == 0) {
            setError(
                QStringLiteral("«%1» es un ajuste de fábrica: elige otro nombre").arg(trimmed));
            return;
        }
    }
    auto list = userPresets();
    const auto it = std::find_if(list.begin(), list.end(),
                                 [&trimmed](const auto& entry) { return entry.first == trimmed; });
    if (it != list.end()) {
        it->second = *style;
    } else {
        list.emplace_back(trimmed, *style);
    }
    storeUserPresets(list);
    emit captionPresetsChanged();
}

void Editor::removeCaptionPreset(const QString& name) {
    auto list = userPresets();
    if (std::erase_if(list, [&name](const auto& entry) { return entry.first == name; }) > 0) {
        storeUserPresets(list);
        emit captionPresetsChanged();
    }
}

sharkcut::CaptionLayout Editor::captionLayoutFrom(const QVariantMap& values) {
    sharkcut::CaptionLayout layout;
    const QString lines =
        values.value(QStringLiteral("lines"), QStringLiteral("double")).toString();
    layout.lines = lines == QLatin1String("single") ? sharkcut::CaptionLayout::Lines::Single
                   : lines == QLatin1String("word") ? sharkcut::CaptionLayout::Lines::Word
                                                    : sharkcut::CaptionLayout::Lines::Double;
    layout.maxChars = std::clamp(values.value(QStringLiteral("maxChars"), 42).toInt(), 1, 200);
    layout.minDurationUs = static_cast<std::int64_t>(
        std::clamp(values.value(QStringLiteral("minSeconds"), 3.0).toDouble(), 0.0, 30.0) *
        1'000'000.0);
    layout.gapFrames = std::clamp(values.value(QStringLiteral("gapFrames"), 0).toInt(), 0, 120);
    layout.removePunctuation = values.value(QStringLiteral("removePunctuation"), false).toBool();
    return layout;
}

CaptionStyle Editor::presetStyle(const QString& name) const {
    for (const QVariant& v : captionPresets()) {
        const QVariantMap preset = v.toMap();
        if (preset.value(QStringLiteral("name")).toString() == name) {
            return styleWith({}, preset.value(QStringLiteral("style")).toMap());
        }
    }
    return {};
}

void Editor::relayoutCaptionTrack(int track, const QVariantMap& values) {
    const sharkcut::CaptionLayout layout = captionLayoutFrom(values);
    if (edit(QStringLiteral("Dividir subtítulos"), [track, &layout](Project& p) {
            return track >= 0 &&
                   sharkcut::relayoutCaptions(p, static_cast<std::size_t>(track), layout);
        })) {
        m_selectedCaptions.clear();
        emit selectionChanged();
    }
}

// --- .srt (en otro hilo: nada de disco en el de la interfaz) ------------------------

void Editor::importSrt(const QUrl& file) {
    const QString path = localPath(file);
    const sharkcut::FrameRate rate = project().settings.frameRate;
    const quint64 projectUid = m_activeProject;
    QPointer<Editor> self(this);
    QThreadPool::globalInstance()->start([self, path, rate, projectUid] {
        QFile in(path);
        std::vector<Caption> captions;
        if (in.open(QIODevice::ReadOnly)) {
            const QByteArray data = in.readAll();
            captions = sharkcut::captionsFromSrt(
                sharkcut::parseSrt(std::string_view(data.constData(), data.size())), rate);
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, captions = std::move(captions), path, projectUid]() mutable {
                if (!self || self->m_activeProject != projectUid) {
                    return;
                }
                if (captions.empty()) {
                    self->setError(QStringLiteral("No se encontraron subtítulos en %1")
                                       .arg(QFileInfo(path).fileName()));
                    return;
                }
                self->edit(QStringLiteral("Importar subtítulos"), [&captions](Project& p) {
                    const std::size_t t = sharkcut::addCaptionTrack(p);
                    sharkcut::setCaptions(p, t, std::move(captions));
                    return true;
                });
            },
            Qt::QueuedConnection);
    });
}

void Editor::exportSrt(int track, const QUrl& file) {
    const Project& p = project();
    if (track < 0 || static_cast<std::size_t>(track) >= p.captionTracks.size()) {
        return;
    }
    const std::string srt = sharkcut::captionsToSrt(
        p.captionTracks[static_cast<std::size_t>(track)], p.settings.frameRate);
    QString path = localPath(file);
    if (!path.endsWith(QLatin1String(".srt"), Qt::CaseInsensitive)) {
        path += QLatin1String(".srt");
    }
    QPointer<Editor> self(this);
    QThreadPool::globalInstance()->start([self, path, srt] {
        QSaveFile out(path);
        const bool ok = out.open(QIODevice::WriteOnly) &&
                        out.write(srt.data(), static_cast<qint64>(srt.size())) ==
                            static_cast<qint64>(srt.size()) &&
                        out.commit();
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, path, ok] {
                if (!self) {
                    return;
                }
                if (ok) {
                    emit self->captionsExported(path);
                } else {
                    self->setError(QStringLiteral("No se pudo guardar %1").arg(path));
                }
            },
            Qt::QueuedConnection);
    });
}
