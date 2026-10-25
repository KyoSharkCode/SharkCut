#include "proxies.h"

#include "core/project_file.h"
#include "media/proxy.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>

#include <exception>
#include <utility>

ProxyManager::ProxyManager(QObject* parent) : QObject(parent) {
    m_pool.setMaxThreadCount(1);
}

ProxyManager::~ProxyManager() {
    m_cancel->store(true);
    m_pool.waitForDone();
}

void ProxyManager::setDirectory(const QString& directory, const QStringList& mediaPaths) {
    if (directory != m_directory) {
        // Otro proyecto: lo que se estaba creando para el anterior se cancela.
        m_cancel->store(true);
        m_cancel = std::make_shared<std::atomic<bool>>(false);
        m_entries.clear();
        m_busy = false;
        ++m_generation;
        m_directory = directory;
    }
    m_mediaPaths = mediaPaths;
    m_scanned = false;
    if (m_directory.isEmpty()) {
        emit changed();
        return;
    }
    // ¿Cuáles tienen ya su proxy? (mirar archivos: fuera del hilo de la interfaz)
    const quint64 generation = m_generation;
    const QString dir = m_directory;
    QPointer<ProxyManager> self(this);
    QThreadPool::globalInstance()->start([self, generation, dir, mediaPaths] {
        std::map<QString, QString> found;
        for (const QString& media : mediaPaths) {
            const std::string name = sharkcut::proxyFileName(media.toStdString());
            if (name.empty()) {
                continue;
            }
            const QString file = QDir(dir).filePath(QString::fromStdString(name));
            if (QFileInfo::exists(file)) {
                found[media] = file;
            }
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, generation, found] {
                if (!self || self->m_generation != generation) {
                    return;
                }
                for (const auto& [media, file] : found) {
                    Entry& entry = self->m_entries[media];
                    if (entry.state == State::None || entry.state == State::Failed) {
                        entry.state = State::Ready;
                        entry.proxyPath = file;
                    }
                }
                self->m_scanned = true;
                emit self->changed();
                self->createMissing();
            },
            Qt::QueuedConnection);
    });
}

QString ProxyManager::readyPath(const QString& mediaPath) const {
    const auto it = m_entries.find(mediaPath);
    return it != m_entries.end() && it->second.state == State::Ready ? it->second.proxyPath
                                                                     : QString();
}

ProxyManager::State ProxyManager::state(const QString& mediaPath) const {
    const auto it = m_entries.find(mediaPath);
    return it == m_entries.end() ? State::None : it->second.state;
}

double ProxyManager::progress(const QString& mediaPath) const {
    const auto it = m_entries.find(mediaPath);
    return it == m_entries.end() ? 0.0 : it->second.progress;
}

int ProxyManager::pending() const {
    int count = 0;
    for (const auto& [path, entry] : m_entries) {
        count += entry.state == State::Queued || entry.state == State::Working;
    }
    return count;
}

int ProxyManager::readyCount(const QStringList& mediaPaths) const {
    int count = 0;
    for (const QString& media : mediaPaths) {
        count += state(media) == State::Ready;
    }
    return count;
}

double ProxyManager::totalProgress() const {
    double sum = 0.0;
    int count = 0;
    for (const auto& [path, entry] : m_entries) {
        if (entry.state == State::Queued || entry.state == State::Working) {
            sum += entry.progress;
            ++count;
        }
    }
    return count > 0 ? sum / count : 0.0;
}

void ProxyManager::create(const QStringList& mediaPaths) {
    if (m_directory.isEmpty()) {
        return;
    }
    for (const QString& media : mediaPaths) {
        Entry& entry = m_entries[media];
        if (entry.state == State::None || entry.state == State::Failed) {
            entry.state = State::Queued;
            entry.progress = 0.0;
        }
    }
    emit changed();
    startNext();
}

void ProxyManager::setAutoCreate(bool on) {
    m_autoCreate = on;
    createMissing();
}

void ProxyManager::createMissing() {
    if (!m_autoCreate || !m_scanned || m_directory.isEmpty()) {
        return;
    }
    QStringList missing;
    for (const QString& media : m_mediaPaths) {
        if (state(media) == State::None) {
            missing << media;
        }
    }
    if (!missing.isEmpty()) {
        create(missing);
    }
}

void ProxyManager::cancelAll() {
    m_autoCreate = false; // cancelados a mano: no vuelven a empezar solos
    m_cancel->store(true);
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    ++m_generation;
    m_busy = false;
    for (auto& [path, entry] : m_entries) {
        if (entry.state == State::Queued || entry.state == State::Working) {
            entry = {};
        }
    }
    emit changed();
}

void ProxyManager::startNext() {
    if (m_busy) {
        return;
    }
    QString media;
    for (auto& [path, entry] : m_entries) {
        if (entry.state == State::Queued) {
            media = path;
            break;
        }
    }
    if (media.isEmpty()) {
        return;
    }
    const std::string name = sharkcut::proxyFileName(media.toStdString());
    Entry& entry = m_entries[media];
    if (name.empty()) {
        entry.state = State::Failed; // el original ya no está
        emit changed();
        startNext();
        return;
    }
    entry.state = State::Working;
    entry.target = QDir(m_directory).filePath(QString::fromStdString(name));
    m_busy = true;
    emit changed();

    const quint64 generation = m_generation;
    const QString target = entry.target;
    const auto cancel = m_cancel;
    QPointer<ProxyManager> self(this);
    auto post = [self, generation](auto fn) {
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, generation, fn = std::move(fn)] {
                if (self && self->m_generation == generation) {
                    fn(*self);
                }
            },
            Qt::QueuedConnection);
    };
    m_pool.start([media, target, cancel, post] {
        bool ok = false;
        try {
            // Ya creado (se pidió antes de terminar de mirar la carpeta): está completo,
            // porque writeProxy solo deja el archivo al acabar.
            if (QFileInfo::exists(target)) {
                ok = true;
            } else {
                sharkcut::writeProxy(
                    media.toStdString(), target.toStdString(),
                    [&post, media](std::int64_t done, std::int64_t total) {
                        const double fraction =
                            total > 0 ? static_cast<double>(done) / static_cast<double>(total)
                                      : 0.0;
                        post([media, fraction](ProxyManager& m) {
                            m.m_entries[media].progress = fraction;
                            if (!m.m_lastProgress.isValid() ||
                                m.m_lastProgress.elapsed() >= kProgressIntervalMs) {
                                m.m_lastProgress.start();
                                emit m.changed();
                            }
                        });
                    },
                    cancel.get());
                ok = true;
            }
        } catch (const std::exception&) {
            ok = false; // archivo sin video, cancelado o sin codificador
        }
        post([media, target, ok](ProxyManager& m) {
            Entry& e = m.m_entries[media];
            e.state = ok ? State::Ready : State::Failed;
            e.progress = ok ? 1.0 : 0.0;
            e.proxyPath = ok ? target : QString();
            m.m_busy = false;
            emit m.changed();
            if (ok) {
                emit m.ready(media);
            }
            m.startNext();
        });
    });
}
