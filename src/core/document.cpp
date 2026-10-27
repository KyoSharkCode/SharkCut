#include "core/document.h"

#include "core/edit.h"

#include <algorithm>
#include <map>

namespace sharkcut {

const Sequence* Document::sequence(ObjectId id) const {
    for (const Sequence& s : sequences) {
        if (s.id == id) {
            return &s;
        }
    }
    return nullptr;
}

const Folder* Document::folder(ObjectId id) const {
    for (const Folder& f : folders) {
        if (f.id == id) {
            return &f;
        }
    }
    return nullptr;
}

Project Document::view(ObjectId id) const {
    Project project;
    project.media = media;
    project.nextId = nextId;
    if (const Sequence* s = sequence(id)) {
        project.settings = s->settings;
        project.tracks = s->tracks;
        project.markers = s->markers;
        project.captionTracks = s->captionTracks;
    }
    return project;
}

void Document::store(ObjectId id, const Project& view) {
    media = view.media;
    nextId = std::max(nextId, view.nextId);
    for (Sequence& s : sequences) {
        if (s.id == id) {
            s.settings = view.settings;
            s.tracks = view.tracks;
            s.markers = view.markers;
            s.captionTracks = view.captionTracks;
        }
    }
}

std::vector<Track> defaultTracks() {
    return {Track{TrackKind::Video, {}}, Track{TrackKind::Video, {}}, Track{TrackKind::Audio, {}},
            Track{TrackKind::Audio, {}}};
}

Document documentFromProject(Project project, std::string sequenceName) {
    Document document;
    document.media = std::move(project.media);
    document.nextId = project.nextId;
    Sequence sequence;
    sequence.id = document.nextId++;
    sequence.name = std::move(sequenceName);
    sequence.settings = project.settings;
    sequence.tracks = std::move(project.tracks);
    sequence.markers = std::move(project.markers);
    sequence.captionTracks = std::move(project.captionTracks);
    document.sequences.push_back(std::move(sequence));
    return document;
}

bool edit(Document& document, ObjectId sequence, const std::function<bool(Project&)>& change) {
    if (!document.sequence(sequence)) {
        return false;
    }
    Project view = document.view(sequence);
    if (!change(view)) {
        return false;
    }
    document.store(sequence, view);
    return true;
}

ObjectId addSequence(Document& document, std::string name, ProjectSettings settings,
                     ObjectId folder) {
    Sequence sequence;
    sequence.id = document.nextId++;
    sequence.name = std::move(name);
    sequence.folder = document.folder(folder) ? folder : 0;
    sequence.settings = settings;
    sequence.tracks = defaultTracks();
    document.sequences.push_back(std::move(sequence));
    return document.sequences.back().id;
}

bool renameSequence(Document& document, ObjectId id, std::string name) {
    for (Sequence& s : document.sequences) {
        if (s.id == id && !name.empty() && s.name != name) {
            s.name = std::move(name);
            return true;
        }
    }
    return false;
}

bool removeSequence(Document& document, ObjectId id) {
    if (document.sequences.size() <= 1) {
        return false;
    }
    return std::erase_if(document.sequences, [id](const Sequence& s) { return s.id == id; }) > 0;
}

ObjectId duplicateSequence(Document& document, ObjectId id, std::string name,
                           std::optional<std::pair<int, int>> canvas) {
    const Sequence* original = document.sequence(id);
    if (!original) {
        return 0;
    }
    Sequence copy = *original;
    copy.id = document.nextId++;
    copy.name = std::move(name);
    if (canvas) {
        copy.settings.width = canvas->first;
        copy.settings.height = canvas->second;
        copy.settings.adoptCanvas = false;
    }
    // Ids nuevos para clips y transiciones; los enlaces apuntan a las copias.
    std::map<ObjectId, ObjectId> renamed;
    for (Track& track : copy.tracks) {
        for (Clip& clip : track.clips) {
            const ObjectId fresh = document.nextId++;
            renamed[clip.id] = fresh;
            clip.id = fresh;
        }
    }
    auto map = [&renamed](ObjectId old) {
        const auto it = renamed.find(old);
        return it == renamed.end() ? ObjectId{0} : it->second;
    };
    for (Track& track : copy.tracks) {
        for (Clip& clip : track.clips) {
            clip.link = map(clip.link);
        }
        for (Transition& t : track.transitions) {
            t.id = document.nextId++;
            t.from = map(t.from);
            t.to = map(t.to);
            t.sound = map(t.sound);
        }
    }
    for (Marker& marker : copy.markers) {
        marker.id = document.nextId++;
    }
    for (CaptionTrack& track : copy.captionTracks) {
        for (Caption& caption : track.captions) {
            caption.id = document.nextId++;
        }
    }
    document.sequences.push_back(std::move(copy));
    return document.sequences.back().id;
}

ObjectId addFolder(Document& document, std::string name) {
    Folder folder;
    folder.id = document.nextId++;
    folder.name = std::move(name);
    document.folders.push_back(std::move(folder));
    return document.folders.back().id;
}

bool renameFolder(Document& document, ObjectId id, std::string name) {
    for (Folder& f : document.folders) {
        if (f.id == id && !name.empty() && f.name != name) {
            f.name = std::move(name);
            f.resources = false; // con otro nombre ya es una carpeta más
            return true;
        }
    }
    return false;
}

ObjectId resourceFolder(Document& document, std::string name) {
    for (const Folder& f : document.folders) {
        if (f.resources) {
            return f.id;
        }
    }
    const ObjectId id = addFolder(document, std::move(name));
    document.folders.back().resources = true;
    return id;
}

bool removeFolder(Document& document, ObjectId id) {
    if (std::erase_if(document.folders, [id](const Folder& f) { return f.id == id; }) == 0) {
        return false;
    }
    for (MediaItem& item : document.media) {
        if (item.folder == id) {
            item.folder = 0;
        }
    }
    for (Sequence& s : document.sequences) {
        if (s.folder == id) {
            s.folder = 0;
        }
    }
    return true;
}

bool moveToFolder(Document& document, const std::vector<ObjectId>& items, ObjectId folder) {
    if (folder != 0 && !document.folder(folder)) {
        return false;
    }
    auto wanted = [&items](ObjectId id) {
        return std::find(items.begin(), items.end(), id) != items.end();
    };
    bool changed = false;
    for (MediaItem& item : document.media) {
        if (wanted(item.id) && item.folder != folder) {
            item.folder = folder;
            changed = true;
        }
    }
    for (Sequence& s : document.sequences) {
        if (wanted(s.id) && s.folder != folder) {
            s.folder = folder;
            changed = true;
        }
    }
    return changed;
}

bool removeMediaEverywhere(Document& document, ObjectId media) {
    if (std::none_of(document.media.begin(), document.media.end(),
                     [media](const MediaItem& m) { return m.id == media; })) {
        return false;
    }
    // Primero sus clips de cada secuencia (con el medio aún en la biblioteca, para que
    // los enlaces y transiciones se pongan al día) y al final el medio.
    for (const Sequence& s : std::vector<Sequence>(document.sequences)) {
        edit(document, s.id, [media](Project& p) {
            std::vector<ObjectId> clips;
            for (const Track& track : p.tracks) {
                for (const Clip& clip : track.clips) {
                    if (clip.media == media) {
                        clips.push_back(clip.id);
                    }
                }
            }
            // Quitar un medio se lleva todos sus clips, también de pistas bloqueadas.
            std::vector<bool> locked;
            for (Track& track : p.tracks) {
                locked.push_back(track.locked);
                track.locked = false;
            }
            removeClips(p, clips);
            for (std::size_t i = 0; i < p.tracks.size(); ++i) {
                p.tracks[i].locked = locked[i];
            }
            return true;
        });
    }
    std::erase_if(document.media, [media](const MediaItem& m) { return m.id == media; });
    return true;
}

} // namespace sharkcut
