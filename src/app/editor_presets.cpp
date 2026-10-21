// Ajustes guardados de clips: el aspecto de un clip (colocación, opacidad, recorte,
// efectos, pantalla verde y su animación) con nombre, en la configuración de la app, para
// aplicarlo a otros clips desde el panel Efectos.

#include "editor.h"

#include "core/clip_preset.h"

#include <QSettings>

#include <algorithm>

using sharkcut::ClipPreset;
using sharkcut::ObjectId;
using sharkcut::PresetAnchor;
using sharkcut::PresetParts;

namespace {

constexpr auto kClipPresetsKey = "efectos/ajustes";

std::vector<ClipPreset> loadClipPresets() {
    const QByteArray json = QSettings().value(QLatin1String(kClipPresetsKey)).toByteArray();
    return sharkcut::presetsFromJson(std::string_view(json.constData(), json.size()));
}

void storeClipPresets(std::vector<ClipPreset> list) {
    std::sort(list.begin(), list.end(), [](const ClipPreset& a, const ClipPreset& b) {
        return QString::fromStdString(a.name).compare(QString::fromStdString(b.name),
                                                      Qt::CaseInsensitive) < 0;
    });
    QSettings().setValue(QLatin1String(kClipPresetsKey),
                         QByteArray::fromStdString(sharkcut::presetsToJson(list)));
}

// "Colocación · Efectos · Animación desde el final"
QString summary(const ClipPreset& preset) {
    const PresetParts& parts = preset.parts;
    QStringList list;
    if (parts.placement) {
        list << QStringLiteral("Colocación");
    }
    if (parts.opacity) {
        list << QStringLiteral("Opacidad");
    }
    if (parts.crop) {
        list << QStringLiteral("Recorte");
    }
    if (parts.effects) {
        list << QStringLiteral("Efectos");
    }
    if (parts.chroma) {
        list << QStringLiteral("Pantalla verde");
    }
    if (preset.animated()) {
        list << (preset.anchor == PresetAnchor::End ? QStringLiteral("Animación desde el final")
                                                    : QStringLiteral("Animación desde el inicio"));
    }
    return list.join(QStringLiteral(" · "));
}

auto byName(const QString& name) {
    return [key = name.toStdString()](const ClipPreset& p) { return p.name == key; };
}

} // namespace

QVariantList Editor::clipPresets() const {
    QVariantList list;
    for (const ClipPreset& p : loadClipPresets()) {
        list.append(QVariantMap{{QStringLiteral("name"), QString::fromStdString(p.name)},
                                {QStringLiteral("parts"),
                                 QVariantMap{{QStringLiteral("placement"), p.parts.placement},
                                             {QStringLiteral("opacity"), p.parts.opacity},
                                             {QStringLiteral("crop"), p.parts.crop},
                                             {QStringLiteral("effects"), p.parts.effects},
                                             {QStringLiteral("chroma"), p.parts.chroma},
                                             {QStringLiteral("animation"), p.parts.animation}}},
                                {QStringLiteral("anchor"), p.anchor == PresetAnchor::End
                                                               ? QStringLiteral("end")
                                                               : QStringLiteral("start")},
                                {QStringLiteral("animated"), p.animated()},
                                {QStringLiteral("summary"), summary(p)}});
    }
    return list;
}

bool Editor::hasClipPreset(const QString& name) const {
    const auto list = loadClipPresets();
    return std::any_of(list.begin(), list.end(), byName(name.trimmed()));
}

bool Editor::saveClipPreset(qint64 clipId, const QString& name, const QVariantMap& options) {
    const QString trimmed = name.trimmed();
    const sharkcut::Clip* clip = project().clip(static_cast<ObjectId>(clipId));
    const auto ref = project().findClip(static_cast<ObjectId>(clipId));
    if (trimmed.isEmpty() || !clip ||
        project().tracks[ref->track].kind != sharkcut::TrackKind::Video) {
        return false;
    }
    PresetParts parts;
    const auto part = [&options](const char* key) {
        return options.value(QLatin1String(key), true).toBool();
    };
    parts.placement = part("placement");
    parts.opacity = part("opacity");
    parts.crop = part("crop");
    parts.effects = part("effects");
    parts.chroma = part("chroma");
    parts.animation = part("animation");
    if (!parts.placement && !parts.opacity && !parts.crop && !parts.effects && !parts.chroma) {
        setError(QStringLiteral("El ajuste no lleva nada: marca al menos una parte"));
        return false;
    }
    const PresetAnchor anchor =
        options.value(QStringLiteral("anchor")).toString() == QLatin1String("end")
            ? PresetAnchor::End
            : PresetAnchor::Start;
    // El valor que se ve en el cabezal (o en el primer fotograma, si está fuera del clip).
    const std::int64_t frame =
        std::clamp<std::int64_t>(m_currentFrame, clip->start, clip->end() - 1);
    ClipPreset preset = sharkcut::presetFromClip(*clip, frame, project().settings.frameRate, parts,
                                                 anchor, trimmed.toStdString());
    auto list = loadClipPresets();
    std::erase_if(list, byName(trimmed));
    list.push_back(std::move(preset));
    storeClipPresets(std::move(list));
    emit clipPresetsChanged();
    return true;
}

void Editor::applyClipPreset(qint64 clipId, const QString& name) {
    const auto list = loadClipPresets();
    const auto it = std::find_if(list.begin(), list.end(), byName(name));
    if (it == list.end()) {
        return;
    }
    const ClipPreset preset = *it;
    // A todos los seleccionados si el clip es uno de ellos.
    const std::vector<ObjectId> targets = effectTargets(static_cast<ObjectId>(clipId));
    const bool changed = edit(QStringLiteral("Aplicar el ajuste «%1»").arg(name),
                              [&targets, &preset](sharkcut::Project& p) {
                                  bool any = false;
                                  for (ObjectId id : targets) {
                                      any = sharkcut::applyPreset(p, id, preset) || any;
                                  }
                                  return any;
                              });
    if (changed) {
        emit effectApplied(QStringLiteral("ajuste:") + name);
    }
}

bool Editor::renameClipPreset(const QString& name, const QString& newName) {
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty() || trimmed == name) {
        return false;
    }
    auto list = loadClipPresets();
    if (std::any_of(list.begin(), list.end(), byName(trimmed))) {
        setError(QStringLiteral("Ya hay un ajuste llamado «%1»").arg(trimmed));
        return false;
    }
    const auto it = std::find_if(list.begin(), list.end(), byName(name));
    if (it == list.end()) {
        return false;
    }
    it->name = trimmed.toStdString();
    storeClipPresets(std::move(list));
    emit clipPresetsChanged();
    return true;
}

void Editor::removeClipPreset(const QString& name) {
    auto list = loadClipPresets();
    if (std::erase_if(list, byName(name)) > 0) {
        storeClipPresets(std::move(list));
        emit clipPresetsChanged();
    }
}
