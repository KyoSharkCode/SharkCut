#include "appinfo.h"

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
