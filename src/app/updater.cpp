#include "updater.h"

#include "core/version.h"
#include "modeldownload.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>

namespace {

constexpr auto kLatestRelease =
    "https://api.github.com/repos/KyoSharkCode/SharkCut/releases/latest";
constexpr auto kReleasesPage = "https://github.com/KyoSharkCode/SharkCut/releases/latest";
constexpr auto kAutoCheckKey = "actualizaciones/automaticas";

// Instalador descargado que se ejecuta al cerrar la app ("" = ninguno).
QString& pendingInstaller() {
    static QString path;
    return path;
}

} // namespace

Updater::Updater(QObject* parent) : QObject(parent), m_network(new QNetworkAccessManager(this)) {}

Updater::~Updater() {
    cancelDownload();
}

QString Updater::currentVersion() const {
    return QStringLiteral(SHARKCUT_VERSION);
}

bool Updater::canInstall() const {
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool Updater::autoCheck() const {
    return QSettings().value(QLatin1String(kAutoCheckKey), true).toBool();
}

void Updater::setAutoCheck(bool on) {
    if (on != autoCheck()) {
        QSettings().setValue(QLatin1String(kAutoCheckKey), on);
        emit changed();
    }
}

void Updater::setState(const QString& state, const QString& message) {
    m_state = state;
    m_message = message;
    emit changed();
}

void Updater::check(bool quiet) {
    if (m_state == QLatin1String("buscando") || m_state == QLatin1String("descargando") ||
        (quiet && !autoCheck())) {
        return;
    }
    setState(QStringLiteral("buscando"), QStringLiteral("Buscando actualizaciones…"));
    // SHARKCUT_UPDATE_FEED: otra dirección con el mismo JSON (para probar sin publicar).
    const QString feed = qEnvironmentVariable("SHARKCUT_UPDATE_FEED");
    QNetworkRequest request{QUrl(feed.isEmpty() ? QString::fromLatin1(kLatestRelease) : feed)};
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", QByteArray("SharkCut/") + SHARKCUT_VERSION);
    request.setTransferTimeout(15'000);
    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, quiet] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 404) {
            // Aún no hay ninguna versión publicada.
            setState(QStringLiteral("aldia"), QStringLiteral("Tienes la última versión"));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            qWarning("Actualizaciones: %s", qPrintable(reply->errorString()));
            setState(quiet ? QString() : QStringLiteral("error"),
                     quiet ? QString()
                           : QStringLiteral("No se pudo comprobar (¿sin conexión?): %1")
                                 .arg(reply->errorString()));
            return;
        }
        const QJsonObject release = QJsonDocument::fromJson(reply->readAll()).object();
        const QString tag = release.value(QStringLiteral("tag_name")).toString();
        if (!sharkcut::isNewerVersion(tag.toStdString(), SHARKCUT_VERSION)) {
            setState(QStringLiteral("aldia"),
                     QStringLiteral("Tienes la última versión (%1)").arg(currentVersion()));
            return;
        }
        m_latest = tag.startsWith(QLatin1Char('v')) ? tag.mid(1) : tag;
        m_notes = release.value(QStringLiteral("body")).toString();
        m_pageUrl = release.value(QStringLiteral("html_url")).toString();
        m_installerUrl.clear();
        m_installerSha256.clear();
        m_installerSize = 0;
        // El instalador de Windows: "SharkCut-<versión>-Setup.exe".
        for (const QJsonValue& value : release.value(QStringLiteral("assets")).toArray()) {
            const QJsonObject asset = value.toObject();
            const QString name = asset.value(QStringLiteral("name")).toString();
            if (name.endsWith(QLatin1String("-Setup.exe"), Qt::CaseInsensitive)) {
                m_installerUrl = asset.value(QStringLiteral("browser_download_url")).toString();
                m_installerSize = asset.value(QStringLiteral("size")).toInteger();
                const QString digest = asset.value(QStringLiteral("digest")).toString();
                if (digest.startsWith(QLatin1String("sha256:"))) {
                    m_installerSha256 = digest.mid(7).toLower();
                }
                m_installerPath =
                    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                        .filePath(QStringLiteral("actualizaciones/") + name);
            }
        }
        qInfo("Actualizaciones: hay una versión nueva, %s", qPrintable(m_latest));
        setState(QStringLiteral("disponible"),
                 QStringLiteral("SharkCut %1 está disponible").arg(m_latest));
        emit updateFound();
    });
}

void Updater::download() {
    if (m_state != QLatin1String("disponible") && m_state != QLatin1String("error")) {
        return;
    }
    // Sin instalador para este sistema, o sin su huella (no se ejecuta nada sin comprobar):
    // la página de la versión.
    if (!canInstall() || m_installerUrl.isEmpty() || m_installerSha256.isEmpty()) {
        openReleasePage();
        return;
    }
    QDir().mkpath(QFileInfo(m_installerPath).absolutePath());
    auto* thread = new QThread;
    auto* download =
        new ModelDownload(QUrl(m_installerUrl), m_installerPath, m_installerSize,
                          m_installerSha256.toLatin1(), QStringLiteral("la actualización"));
    download->moveToThread(thread);
    m_thread = thread;
    m_download = download;
    m_progress = 0.0;
    setState(QStringLiteral("descargando"),
             QStringLiteral("Descargando SharkCut %1…").arg(m_latest));
    connect(thread, &QThread::started, download, &ModelDownload::start);
    connect(download, &ModelDownload::progress, this, [this](double fraction) {
        m_progress = fraction;
        emit changed();
    });
    connect(
        download, &ModelDownload::finished, this, [this, thread](bool ok, const QString& error) {
            thread->quit();
            m_thread = nullptr;
            m_download = nullptr;
            if (ok) {
                setState(QStringLiteral("lista"),
                         QStringLiteral("SharkCut %1 está listo para instalarse").arg(m_latest));
            } else {
                setState(error.isEmpty() ? QStringLiteral("disponible") : QStringLiteral("error"),
                         error);
            }
        });
    connect(thread, &QThread::finished, download, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void Updater::cancelDownload() {
    QThread* thread = m_thread;
    if (m_download) {
        m_download->cancel();
    }
    if (thread) {
        thread->quit();
        thread->wait();
    }
}

void Updater::installOnExit() {
    if (m_state == QLatin1String("lista")) {
        pendingInstaller() = m_installerPath;
    }
}

void Updater::openReleasePage() const {
    QDesktopServices::openUrl(
        QUrl(m_pageUrl.isEmpty() ? QString::fromLatin1(kReleasesPage) : m_pageUrl));
}

void Updater::runPendingInstaller() {
    const QString path = pendingInstaller();
    if (path.isEmpty()) {
        return;
    }
    // Instalación con su barra de progreso, sin preguntas; al terminar abre SharkCut.
    qInfo("Actualizaciones: se instala %s", qPrintable(path));
    QProcess::startDetached(
        path, {QStringLiteral("/SP-"), QStringLiteral("/SILENT"), QStringLiteral("/NOCANCEL")});
}
