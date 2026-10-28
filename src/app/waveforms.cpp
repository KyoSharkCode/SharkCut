#include "waveforms.h"

#include "core/project_file.h"
#include "editor.h"

#include <QCoreApplication>
#include <QDir>
#include <QLineF>
#include <QMetaObject>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <exception>
#include <vector>

namespace {

std::filesystem::path cacheFile(const QString& directory, const QString& mediaPath,
                                int audioStream) {
    const std::string name = sharkcut::waveformCacheName(mediaPath.toStdString(), audioStream);
    if (directory.isEmpty() || name.empty()) {
        return {};
    }
    return sharkcut::pathFromUtf8(directory.toStdString()) / name;
}

} // namespace

// --- WaveformCache -------------------------------------------------------------------

WaveformCache::WaveformCache(QObject* parent) : QObject(parent) {
    // Decodificar el audio de un VOD entero lleva unos segundos (y cada onda ya se reparte
    // en varios hilos): pocas a la vez.
    m_pool.setMaxThreadCount(2);
}

WaveformCache::~WaveformCache() {
    m_cancel->store(true);
    m_pool.waitForDone();
}

std::shared_ptr<const sharkcut::Waveform> WaveformCache::get(const QString& mediaPath,
                                                             int audioStream) {
    const Key key{mediaPath, audioStream};
    Entry& entry = m_entries[key];
    if (entry.waveform || entry.pending || entry.failed) {
        return entry.waveform;
    }
    entry.pending = true;
    const QString directory = m_directory;
    const auto cancel = m_cancel;
    QPointer<WaveformCache> self(this);
    m_pool.start([self, key, mediaPath, audioStream, directory, cancel] {
        std::shared_ptr<const sharkcut::Waveform> result;
        const std::filesystem::path file = cacheFile(directory, mediaPath, audioStream);
        try {
            if (auto cached = file.empty() ? std::nullopt : sharkcut::loadWaveform(file)) {
                result = std::make_shared<const sharkcut::Waveform>(std::move(*cached));
            } else {
                result = std::make_shared<const sharkcut::Waveform>(sharkcut::computeWaveform(
                    mediaPath.toStdString(), cancel.get(), 0, audioStream));
                if (!file.empty()) {
                    sharkcut::saveWaveform(*result, file);
                }
            }
        } catch (const std::exception&) {
            // sin audio, archivo que falta o cancelado: sin onda
        }
        if (cancel->load()) {
            return;
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, key, result] {
                if (self) {
                    self->finish(key, result);
                }
            },
            Qt::QueuedConnection);
    });
    return nullptr;
}

void WaveformCache::finish(const Key& key, std::shared_ptr<const sharkcut::Waveform> waveform) {
    Entry& entry = m_entries[key];
    entry.pending = false;
    entry.failed = !waveform;
    entry.waveform = std::move(waveform);
    emit ready(key.first);
}

void WaveformCache::setDirectory(const QString& directory) {
    if (directory == m_directory) {
        return;
    }
    m_directory = directory;
}

void WaveformCache::saveTo(const QString& directory, const QStringList& mediaPaths) {
    if (directory.isEmpty()) {
        return;
    }
    for (const auto& [key, entry] : m_entries) {
        if (entry.waveform && mediaPaths.contains(key.first)) {
            save(directory, key, entry.waveform);
        }
    }
}

bool WaveformCache::pending(const QString& mediaPath, int audioStream) const {
    const auto it = m_entries.find(Key{mediaPath, audioStream});
    return it != m_entries.end() && it->second.pending;
}

void WaveformCache::save(const QString& directory, const Key& key,
                         std::shared_ptr<const sharkcut::Waveform> waveform) {
    m_pool.start([key, directory, waveform] {
        const std::filesystem::path file = cacheFile(directory, key.first, key.second);
        std::error_code ignored;
        if (!file.empty() && !std::filesystem::exists(file, ignored)) {
            sharkcut::saveWaveform(*waveform, file);
        }
    });
}

// --- WaveformView --------------------------------------------------------------------

WaveformView::WaveformView(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(false);
    connect(this, &WaveformView::changed, this, [this] { update(); });
}

template <typename T> void WaveformView::set(T& member, const T& value) {
    if (member != value) {
        member = value;
        emit changed();
    }
}

void WaveformView::setEditor(Editor* editor) {
    if (m_editor == editor) {
        return;
    }
    if (m_editor) {
        disconnect(m_editor, nullptr, this, nullptr);
    }
    m_editor = editor;
    if (m_editor) {
        connect(m_editor, &Editor::waveformsChanged, this, &WaveformView::refresh);
    }
    refresh();
    emit changed();
}

void WaveformView::refresh() {
    m_waveform = m_editor ? m_editor->waveform(m_mediaId, m_audioStream) : nullptr;
    const bool loading =
        !m_waveform && m_editor && m_editor->waveformLoading(m_mediaId, m_audioStream);
    if (loading != m_loading) {
        m_loading = loading;
        emit loadingChanged();
    }
    update();
}

void WaveformView::setMediaId(qint64 id) {
    if (m_mediaId != id) {
        m_mediaId = id;
        refresh();
        emit changed();
    }
}
void WaveformView::setAudioStream(int stream) {
    if (m_audioStream != stream) {
        m_audioStream = stream;
        refresh();
        emit changed();
    }
}
void WaveformView::setFirstFrame(double frame) {
    set(m_firstFrame, frame);
}
void WaveformView::setFramesPerPixel(double value) {
    set(m_framesPerPixel, std::max(value, 1e-6));
}
void WaveformView::setFps(double fps) {
    set(m_fps, std::max(fps, 1.0));
}
void WaveformView::setGainDb(double db) {
    set(m_gainDb, db);
}
void WaveformView::setColor(const QColor& color) {
    set(m_color, color);
}

void WaveformView::paint(QPainter* painter) {
    const std::shared_ptr<const sharkcut::Waveform> waveform = m_waveform;
    if (!waveform || waveform->peaks.empty()) {
        return;
    }
    const auto& peaks = waveform->peaks;
    const auto count = static_cast<std::int64_t>(peaks.size());
    const double blocksPerFrame = sharkcut::Waveform::kBlocksPerSecond / m_fps;
    // De abajo arriba: silencio = nada, 0 dB = toda la altura.
    const double bottom = height();
    const double maxHeight = std::max(0.0, height() - 1.0);
    const int columns = static_cast<int>(std::ceil(width()));

    // Una línea vertical por columna de píxeles: el pico del tramo que cubre.
    std::vector<QLineF> lines;
    lines.reserve(static_cast<std::size_t>(std::max(columns, 0)));
    for (int x = 0; x < columns; ++x) {
        const double from = (m_firstFrame + x * m_framesPerPixel) * blocksPerFrame;
        const double to = (m_firstFrame + (x + 1) * m_framesPerPixel) * blocksPerFrame;
        auto first = static_cast<std::int64_t>(std::floor(from));
        auto last = std::max(first + 1, static_cast<std::int64_t>(std::ceil(to)));
        first = std::clamp<std::int64_t>(first, 0, count);
        last = std::clamp<std::int64_t>(last, 0, count);
        if (first >= last) {
            continue;
        }
        const std::uint8_t peak = *std::max_element(peaks.begin() + first, peaks.begin() + last);
        const double level = sharkcut::Waveform::level(peak, m_gainDb) * maxHeight;
        if (level < 0.5) {
            continue;
        }
        lines.emplace_back(x + 0.5, bottom, x + 0.5, bottom - level);
    }
    painter->setPen(QPen(m_color, 1.0));
    painter->drawLines(lines.data(), static_cast<int>(lines.size()));
}
