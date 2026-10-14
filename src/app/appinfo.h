#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

// Datos de la aplicación expuestos a QML (versión, Qt, sistema).
class AppInfo : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT)
    Q_PROPERTY(QString operatingSystem READ operatingSystem CONSTANT)
    Q_PROPERTY(QString buildType READ buildType CONSTANT)

public:
    explicit AppInfo(QObject* parent = nullptr);

    QString appVersion() const;
    QString qtVersion() const;
    QString operatingSystem() const;
    QString buildType() const;
};
