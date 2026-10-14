#include "appinfo.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QSysInfo>
#include <QtGlobal>

AppInfo::AppInfo(QObject* parent) : QObject(parent) {}

QString AppInfo::appVersion() const {
    return QStringLiteral(SHARKCUT_VERSION);
}

QString AppInfo::qtVersion() const {
    return QString::fromLatin1(qVersion());
}

QString AppInfo::operatingSystem() const {
    return QSysInfo::prettyProductName();
}

QString AppInfo::buildType() const {
#ifdef NDEBUG
    return QStringLiteral("Release");
#else
    return QStringLiteral("Debug");
#endif
}

QString AppInfo::initialFile() const {
    const QStringList files = initialFiles();
    return files.isEmpty() ? QString() : files.first();
}

QStringList AppInfo::initialFiles() const {
    QStringList files;
    const QStringList args = QCoreApplication::arguments();
    for (qsizetype i = 1; i < args.size(); ++i) {
        files << QFileInfo(args.at(i)).absoluteFilePath();
    }
    return files;
}
