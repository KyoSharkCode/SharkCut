#include "modeldownload.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

ModelDownload::ModelDownload(QUrl url, QString target, qint64 size, QByteArray sha256Hex)
    : m_url(std::move(url)), m_target(std::move(target)), m_size(size),
      m_sha256(std::move(sha256Hex)) {}

void ModelDownload::start() {
    QDir().mkpath(QFileInfo(m_target).absolutePath());
    m_file.setFileName(m_target + QStringLiteral(".part"));
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(QStringLiteral("No se puede escribir en %1").arg(QFileInfo(m_target).absolutePath()));
        return;
    }
    m_network = new QNetworkAccessManager(this);
    QNetworkRequest request(m_url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, &ModelDownload::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &ModelDownload::onFinished);
}

void ModelDownload::cancel() {
    m_cancelled = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            if (m_reply) {
                m_reply->abort();
            }
        },
        Qt::QueuedConnection);
}

void ModelDownload::onReadyRead() {
    const QByteArray data = m_reply->readAll();
    if (m_file.write(data) != data.size()) {
        m_reply->abort();
        fail(QStringLiteral("No se pudo guardar el modelo (¿disco lleno?)"));
        return;
    }
    m_hash.addData(data);
    m_received += data.size();
    emit progress(m_size > 0 ? static_cast<double>(m_received) / static_cast<double>(m_size) : 0.0);
}

void ModelDownload::onFinished() {
    if (m_done) {
        return;
    }
    if (m_cancelled) {
        fail(QStringLiteral("Descarga cancelada"));
        return;
    }
    if (m_reply->error() != QNetworkReply::NoError) {
        fail(QStringLiteral("No se pudo descargar el modelo: %1").arg(m_reply->errorString()));
        return;
    }
    onReadyRead(); // lo que quede
    m_file.close();
    if (m_received != m_size || m_hash.result().toHex() != m_sha256) {
        fail(QStringLiteral("El modelo descargado está incompleto o dañado; vuelve a intentarlo"));
        return;
    }
    QFile::remove(m_target);
    if (!QFile::rename(m_file.fileName(), m_target)) {
        fail(QStringLiteral("No se pudo guardar el modelo en %1").arg(m_target));
        return;
    }
    m_done = true;
    emit finished(true, {});
}

void ModelDownload::fail(const QString& error) {
    if (m_done) {
        return;
    }
    m_done = true;
    m_file.close();
    m_file.remove();
    emit finished(false, error);
}
