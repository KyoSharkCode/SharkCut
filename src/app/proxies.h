#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QThreadPool>

#include <atomic>
#include <map>
#include <memory>

// Proxies de los medios del proyecto activo (ver media/proxy.h), en su caché
// (<proyecto>.sharkp.cache/proxies/). Se crean de uno en uno en segundo plano.
class ProxyManager : public QObject {
    Q_OBJECT

public:
    enum class State { None, Queued, Working, Ready, Failed };

    explicit ProxyManager(QObject* parent = nullptr);
    ~ProxyManager() override; // cancela lo pendiente y espera

    // Carpeta de proxies ("" = el proyecto no tiene archivo: no hay proxies) y los medios
    // del proyecto, para saber cuáles tienen ya el suyo (se mira en segundo plano).
    void setDirectory(const QString& directory, const QStringList& mediaPaths);
    QString directory() const { return m_directory; }

    // Proxy listo de un medio ("" si no hay).
    QString readyPath(const QString& mediaPath) const;
    State state(const QString& mediaPath) const;
    double progress(const QString& mediaPath) const; // 0–1 mientras se crea

    // Pone en cola los que no tengan proxy (ni estén ya en cola).
    void create(const QStringList& mediaPaths);
    void cancelAll();

    // Trabajos pendientes (en cola o en marcha) y su progreso conjunto (0–1).
    int pending() const;
    double totalProgress() const;

signals:
    void changed();
    // Un proxy terminó: si se están usando proxies, hay que reabrir ese medio.
    void ready(const QString& mediaPath);

private:
    struct Entry {
        State state = State::None;
        double progress = 0.0;
        QString proxyPath;
        QString target; // dónde se escribe (al ponerlo en cola)
    };
    void startNext();

    QString m_directory;
    std::map<QString, Entry> m_entries;
    QThreadPool m_pool; // un hilo: crear proxies ya usa toda la máquina
    std::shared_ptr<std::atomic<bool>> m_cancel = std::make_shared<std::atomic<bool>>(false);
    quint64 m_generation = 0; // cambia con la carpeta: descarta resultados viejos
    bool m_busy = false;
};
