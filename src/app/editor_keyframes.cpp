// Keyframes de los clips de video: lo que necesita Propiedades y las acciones en el
// cabezal (animar una propiedad, poner o quitar keyframes, saltar entre ellos, su curva).

#include "editor.h"

#include "core/edit.h"

#include <algorithm>
#include <optional>

using sharkcut::AnimProperty;
using sharkcut::Clip;
using sharkcut::ObjectId;
using sharkcut::Project;

namespace {

std::optional<AnimProperty> propertyOf(const QString& name) {
    return sharkcut::animPropertyFromId(name.toStdString());
}

int easingIndex(sharkcut::Easing easing) {
    const auto& list = sharkcut::kKeyframeEasings;
    const auto it = std::find(list.begin(), list.end(), easing);
    return it == list.end() ? -1 : static_cast<int>(it - list.begin());
}

} // namespace

QVariantMap Editor::animationInfo(const Clip& clip) const {
    QVariantMap info;
    const std::int64_t here = clip.mediaFrameAt(m_currentFrame);
    int keyEasing = -1;
    for (std::size_t i = 0; i < sharkcut::kAnimPropertyCount; ++i) {
        const auto& keys = clip.animation.keys[i];
        bool key = false;
        bool prev = false;
        bool next = false;
        for (const sharkcut::Keyframe& k : keys) {
            if (k.frame == here) {
                key = true;
                if (keyEasing < 0) {
                    keyEasing = easingIndex(k.easing);
                }
            }
            prev = prev || k.frame < here;
            next = next || k.frame > here;
        }
        info.insert(
            QString::fromLatin1(sharkcut::kAnimPropertyIds[i].data(),
                                static_cast<qsizetype>(sharkcut::kAnimPropertyIds[i].size())),
            QVariantMap{{QStringLiteral("animated"), !keys.empty()},
                        {QStringLiteral("key"), key},
                        {QStringLiteral("prev"), prev},
                        {QStringLiteral("next"), next}});
    }
    info.insert(QStringLiteral("keyEasing"), keyEasing);
    info.insert(QStringLiteral("any"), !clip.animation.empty());
    return info;
}

void Editor::setPropertyAnimated(qint64 clipId, const QString& property, bool animated) {
    const auto p = propertyOf(property);
    if (!p) {
        return;
    }
    const auto id = static_cast<ObjectId>(clipId);
    const std::int64_t frame = m_currentFrame;
    m_previewClip = 0;
    edit(animated ? QStringLiteral("Animar") : QStringLiteral("Dejar de animar"),
         [id, p, animated, frame](Project& project) {
             return sharkcut::setPropertyAnimated(project, id, *p, animated, frame);
         });
}

void Editor::toggleKeyframe(qint64 clipId, const QString& property) {
    const auto p = propertyOf(property);
    const Clip* clip = project().clip(static_cast<ObjectId>(clipId));
    if (!p || !clip) {
        return;
    }
    const auto id = clip->id;
    const std::int64_t frame = m_currentFrame;
    const std::int64_t here = clip->mediaFrameAt(frame);
    const auto& keys = clip->animation.of(*p);
    const bool exists = std::any_of(
        keys.begin(), keys.end(), [here](const sharkcut::Keyframe& k) { return k.frame == here; });
    const double value = sharkcut::transformField(sharkcut::transformAt(*clip, frame), *p);
    m_previewClip = 0;
    if (exists) {
        edit(QStringLiteral("Quitar keyframe"), [id, p, frame](Project& project) {
            return sharkcut::removeKeyframe(project, id, *p, frame);
        });
    } else {
        edit(QStringLiteral("Añadir keyframe"), [id, p, frame, value](Project& project) {
            return sharkcut::setKeyframe(project, id, *p, frame, value);
        });
    }
}

void Editor::goToKeyframe(qint64 clipId, const QString& property, int direction) {
    const Clip* clip = project().clip(static_cast<ObjectId>(clipId));
    if (!clip || direction == 0) {
        return;
    }
    const auto p = propertyOf(property);
    const std::int64_t here = clip->mediaFrameAt(m_currentFrame);
    std::optional<std::int64_t> best;
    for (std::size_t i = 0; i < sharkcut::kAnimPropertyCount; ++i) {
        if (p && static_cast<std::size_t>(*p) != i) {
            continue;
        }
        for (const sharkcut::Keyframe& k : clip->animation.keys[i]) {
            const bool ahead = direction > 0 ? k.frame > here : k.frame < here;
            if (ahead && (!best || (direction > 0 ? k.frame < *best : k.frame > *best))) {
                best = k.frame;
            }
        }
    }
    if (best) {
        pause();
        seek(clip->start + (*best - clip->in));
    }
}

void Editor::setKeyframeEasing(qint64 clipId, int index) {
    if (index < 0 || index >= static_cast<int>(sharkcut::kKeyframeEasings.size())) {
        return;
    }
    const auto id = static_cast<ObjectId>(clipId);
    const sharkcut::Easing easing = sharkcut::kKeyframeEasings[static_cast<std::size_t>(index)];
    const std::int64_t frame = m_currentFrame;
    edit(QStringLiteral("Curva del keyframe"), [id, frame, easing](Project& project) {
        return sharkcut::setKeyframeEasing(project, id, frame, easing);
    });
}

void Editor::moveKeyframes(qint64 clipId, qint64 from, qint64 to) {
    const auto id = static_cast<ObjectId>(clipId);
    edit(QStringLiteral("Mover keyframe"), [id, from, to](Project& project) {
        return sharkcut::moveKeyframes(project, id, from, to);
    });
}
