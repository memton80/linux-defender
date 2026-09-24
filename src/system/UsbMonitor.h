#pragma once

#include <QDBusConnection>
#include <QHash>
#include <QObject>
#include <QStringList>

#include <functional>

class QDBusMessage;

/**
 * Détecte le montage des volumes amovibles (clés USB, cartes SD...) grâce à
 * UDisks2, le service système qui gère les disques, via D-Bus.
 *
 * Quand un système de fichiers est monté, UDisks2 émet PropertiesChanged sur
 * son objet (propriété MountPoints de l'interface Filesystem). On vérifie
 * alors que le disque est amovible (propriété Removable de son Drive) avant
 * d'émettre removableMounted().
 *
 * Remarque : sous Plasma, une clé est montée quand on l'ouvre (Dolphin, notifications
 * de périphériques), ou dès le branchement si le montage automatique est activé.
 */
class UsbMonitor : public QObject
{
    Q_OBJECT

public:
    // `bus` : le bus système en temps normal ; les tests utilisent un autre bus.
    explicit UsbMonitor(const QDBusConnection &bus = QDBusConnection::systemBus(), QObject *parent = nullptr);

    // false si UDisks2 n'est pas joignable : aucune clé ne sera détectée.
    bool isAvailable() const;

signals:
    void removableMounted(const QString &mountPoint);

private slots:
    void onPropertiesChanged(const QDBusMessage &message);

private:
    void checkRemovable(const QString &blockObject, const QStringList &mountPoints);
    void getProperty(const QString &object, const QString &interface, const QString &name,
                     const std::function<void(const QVariant &value)> &onValue);

    QDBusConnection m_bus;
    bool m_available = false;
    QHash<QString, QStringList> m_mountPoints; // objet UDisks2 -> points de montage connus
};
