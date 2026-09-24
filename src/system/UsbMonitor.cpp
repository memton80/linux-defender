#include "UsbMonitor.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDebug>
#include <QFile>

namespace
{
const QString kService = QStringLiteral("org.freedesktop.UDisks2");
const QString kPropertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
const QString kBlockInterface = QStringLiteral("org.freedesktop.UDisks2.Block");
const QString kDriveInterface = QStringLiteral("org.freedesktop.UDisks2.Drive");
const QString kFilesystemInterface = QStringLiteral("org.freedesktop.UDisks2.Filesystem");

// MountPoints est de type « aay » : une liste de chemins en octets, chacun
// terminé par un octet nul.
QStringList decodeMountPoints(const QVariant &value)
{
    QStringList mountPoints;
    const QByteArrayList raw = qdbus_cast<QByteArrayList>(value);
    for (QByteArray path : raw) {
        if (path.endsWith('\0'))
            path.chop(1);
        if (!path.isEmpty())
            mountPoints.append(QFile::decodeName(path));
    }
    return mountPoints;
}
}

UsbMonitor::UsbMonitor(const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
{
    // Chemin d'objet vide : on écoute PropertiesChanged sur tous les objets d'UDisks2.
    m_available = m_bus.isConnected()
        && m_bus.connect(kService, QString(), kPropertiesInterface, QStringLiteral("PropertiesChanged"), this,
                         SLOT(onPropertiesChanged(QDBusMessage)));
    if (!m_available)
        qWarning() << "UDisks2 injoignable, les clés USB ne seront pas détectées :" << m_bus.lastError().message();
}

bool UsbMonitor::isAvailable() const
{
    return m_available;
}

void UsbMonitor::onPropertiesChanged(const QDBusMessage &message)
{
    // Arguments : nom de l'interface, propriétés modifiées (a{sv}), propriétés invalidées.
    const QList<QVariant> arguments = message.arguments();
    if (arguments.size() < 2 || arguments.at(0).toString() != kFilesystemInterface)
        return;
    const QVariantMap changed = qdbus_cast<QVariantMap>(arguments.at(1));
    if (!changed.contains(QStringLiteral("MountPoints")))
        return;

    const QString object = message.path();
    const QStringList mountPoints = decodeMountPoints(changed.value(QStringLiteral("MountPoints")));
    const QStringList previous = m_mountPoints.value(object);
    if (mountPoints.isEmpty())
        m_mountPoints.remove(object); // démonté
    else
        m_mountPoints.insert(object, mountPoints);

    QStringList added;
    for (const QString &mountPoint : mountPoints) {
        if (!previous.contains(mountPoint))
            added.append(mountPoint);
    }
    if (!added.isEmpty())
        checkRemovable(object, added);
}

void UsbMonitor::checkRemovable(const QString &blockObject, const QStringList &mountPoints)
{
    // Le volume (Block) indique son disque physique (Drive), qui dit s'il est amovible.
    getProperty(blockObject, kBlockInterface, QStringLiteral("Drive"), [this, mountPoints](const QVariant &drive) {
        const QString driveObject = qvariant_cast<QDBusObjectPath>(drive).path();
        if (driveObject.isEmpty() || driveObject == QLatin1String("/"))
            return; // pas de disque physique : image montée, périphérique loop...

        getProperty(driveObject, kDriveInterface, QStringLiteral("Removable"), [this, mountPoints](const QVariant &removable) {
            if (!removable.toBool())
                return;
            for (const QString &mountPoint : mountPoints)
                emit removableMounted(mountPoint);
        });
    });
}

void UsbMonitor::getProperty(const QString &object, const QString &interface, const QString &name,
                             const std::function<void(const QVariant &value)> &onValue)
{
    // Appel asynchrone : l'interface ne se fige jamais en attendant UDisks2.
    QDBusMessage call = QDBusMessage::createMethodCall(kService, object, kPropertiesInterface, QStringLiteral("Get"));
    call << interface << name;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [onValue](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        const QDBusPendingReply<QDBusVariant> reply = *watcher;
        if (reply.isError()) {
            qWarning() << "UDisks2 :" << reply.error().message();
            return;
        }
        onValue(reply.value().variant());
    });
}
