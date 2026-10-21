#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QNetworkAccessManager;
class QThread;
class ModelDownload;

// Actualizaciones: mira la última versión publicada en GitHub Releases y, si es posterior,
// la ofrece. En Windows descarga su instalador (comprobando el SHA-256 que da GitHub) y lo
// ejecuta al cerrar SharkCut (tras guardar o descartar los proyectos); el instalador
// vuelve a abrir la app. En otros sistemas abre la página de la versión.
class Updater : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // "" (sin mirar) | "buscando" | "aldia" | "disponible" | "descargando" | "lista" | "error"
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QString notes READ notes NOTIFY changed) // novedades (Markdown)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(bool canInstall READ canInstall CONSTANT) // hay instalador para este sistema
    // Buscar al abrir la app (QSettings "actualizaciones/automaticas", sí por defecto).
    Q_PROPERTY(bool autoCheck READ autoCheck WRITE setAutoCheck NOTIFY changed)

public:
    explicit Updater(QObject* parent = nullptr);
    ~Updater() override;

    QString state() const { return m_state; }
    QString currentVersion() const;
    QString latestVersion() const { return m_latest; }
    QString notes() const { return m_notes; }
    QString message() const { return m_message; }
    double progress() const { return m_progress; }
    bool canInstall() const;
    bool autoCheck() const;
    void setAutoCheck(bool on);

    // Busca una versión nueva. `quiet`: al abrir la app (sin mensajes si no hay nada).
    Q_INVOKABLE void check(bool quiet = false);
    // Descarga el instalador de la versión nueva (o abre su página si no hay instalador).
    Q_INVOKABLE void download();
    Q_INVOKABLE void cancelDownload();
    // Instalar al cerrar SharkCut (state "lista").
    Q_INVOKABLE void installOnExit();
    Q_INVOKABLE void openReleasePage() const;

    // Si se pidió instalar al cerrar: lanza el instalador (main, al salir del bucle).
    static void runPendingInstaller();

signals:
    void changed();
    // Hay versión nueva (al buscar al abrir: para avisar una vez).
    void updateFound();

private:
    void setState(const QString& state, const QString& message = {});

    QNetworkAccessManager* m_network = nullptr;
    QString m_state;
    QString m_latest;
    QString m_notes;
    QString m_message;
    QString m_pageUrl;
    QString m_installerUrl;
    QString m_installerSha256;
    qint64 m_installerSize = 0;
    QString m_installerPath;
    double m_progress = 0.0;
    QThread* m_thread = nullptr;
    QPointer<ModelDownload> m_download;
};
