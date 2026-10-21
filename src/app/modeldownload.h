#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <atomic>

class QNetworkAccessManager;
class QNetworkReply;

// Descarga de un archivo (el modelo de transcripción, el instalador de una actualización)
// a `target`, en su propio hilo (la escritura en disco no toca el de la interfaz). Se
// guarda primero como "<target>.part" y solo se renombra si el tamaño y el SHA-256
// coinciden. `what` lo nombra en los mensajes de error ("el modelo").
//
// Uso: moverlo a un QThread, llamar a start() en ese hilo (p. ej. al arrancar el hilo) y
// esperar finished(). cancel() se puede llamar desde cualquier hilo.
class ModelDownload : public QObject {
    Q_OBJECT
public:
    ModelDownload(QUrl url, QString target, qint64 size, QByteArray sha256Hex,
                  QString what = QStringLiteral("el modelo"));

    void start();
    void cancel();

signals:
    void progress(double fraction);
    void finished(bool ok, const QString& error);

private:
    void onReadyRead();
    void onFinished();
    void fail(const QString& error);

    QUrl m_url;
    QString m_target;
    qint64 m_size = 0;
    QByteArray m_sha256;
    QString m_what;
    QNetworkAccessManager* m_network = nullptr;
    QPointer<QNetworkReply> m_reply;
    QFile m_file;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    qint64 m_received = 0;
    std::atomic<bool> m_cancelled{false};
    bool m_done = false;
};
