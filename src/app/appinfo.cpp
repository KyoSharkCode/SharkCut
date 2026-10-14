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
    const QStringList args = QCoreApplication::arguments();
    if (args.size() < 2) {
        return {};
    }
    return QFileInfo(args.at(1)).absoluteFilePath();
}
