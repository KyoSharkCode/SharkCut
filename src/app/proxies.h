#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThreadPool>

#include <atomic>
#include <map>
#include <memory>

// Proxies de los medios del proyecto activo (ver media/proxy.h), en la caché común de
// SharkCut (<caché>/proxies/, por archivo: los comparten los proyectos que lo usan); los
// de la caché antigua del proyecto (<proyecto>.sharkp.cache/proxies/) también se usan.
// Se crean de uno en uno en segundo plano.
class ProxyManager : public QObject {
    Q_OBJECT

public:
    enum class State { None, Queued, Working, Ready, Failed };

    explicit ProxyManager(QObject* parent = nullptr);
    ~ProxyManager() override; // cancela lo pendiente y espera

    // Carpeta de proxies ("" = no hay proxies), la antigua del proyecto (solo se lee; ""
    // = ninguna) y los medios del proyecto, para saber cuáles tienen ya el suyo (se mira
    // en segundo plano).
    void setDirectory(const QString& directory, const QStringList& mediaPaths,
                      const QString& legacyDirectory = {});
    // Olvida los proxies listos (al vaciar la caché) y vuelve a mirar cuáles existen.
    void rescan();
    QString directory() const { return m_directory; }

    // Proxy listo de un medio ("" si no hay).
    QString readyPath(const QString& mediaPath) const;
    State state(const QString& mediaPath) const;
    double progress(const QString& mediaPath) const; // 0–1 mientras se crea

    // Pone en cola los que no tengan proxy (ni estén ya en cola).
    void create(const QStringList& mediaPaths);
    // Crear solos los proxies que falten (al activarlos, al abrir un proyecto o al importar
    // videos), tras mirar cuáles existen. Los que fallaron no se reintentan solos, y
    // cancelAll() lo desactiva hasta la próxima vez.
    void setAutoCreate(bool on);
    void cancelAll();

    // Trabajos pendientes (en cola o en marcha) y su progreso conjunto (0–1).
    int pending() const;
    // Cuántos de esos medios tienen su proxy listo.
    int readyCount(const QStringList& mediaPaths) const;
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

    void createMissing();

    QString m_directory;
    QString m_legacyDirectory;
    QStringList m_mediaPaths; // los del último setDirectory()
    bool m_autoCreate = false;
    bool m_scanned = false; // ya se miró qué proxies existen en la carpeta
    std::map<QString, Entry> m_entries;
    QThreadPool m_pool; // un hilo: crear proxies ya usa toda la máquina
    std::shared_ptr<std::atomic<bool>> m_cancel = std::make_shared<std::atomic<bool>>(false);
    quint64 m_generation = 0; // cambia con la carpeta: descarta resultados viejos
    bool m_busy = false;
    // El progreso se anuncia como mucho cada kProgressIntervalMs (cada aviso repinta la
    // interfaz); los cambios de estado, siempre.
    static constexpr qint64 kProgressIntervalMs = 250;
    QElapsedTimer m_lastProgress;
};
