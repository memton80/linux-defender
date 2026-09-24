#include "system/UsbMonitor.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QSignalSpy>
#include <QTest>

// Faux UDisks2, publié sur le bus de session sous le vrai nom de service.
// Il n'expose que les propriétés que lit UsbMonitor.

class FakeBlock : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.UDisks2.Block")
    Q_PROPERTY(QDBusObjectPath Drive READ drive)

public:
    FakeBlock(QObject *parent, const QString &drive)
        : QDBusAbstractAdaptor(parent)
        , m_drive(drive)
    {
    }
    QDBusObjectPath drive() const { return QDBusObjectPath(m_drive); }

private:
    QString m_drive;
};

class FakeDrive : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.UDisks2.Drive")
    Q_PROPERTY(bool Removable READ removable)

public:
    FakeDrive(QObject *parent, bool removable)
        : QDBusAbstractAdaptor(parent)
        , m_removable(removable)
    {
    }
    bool removable() const { return m_removable; }

private:
    bool m_removable;
};

namespace
{
const QString kUsbBlock = QStringLiteral("/org/freedesktop/UDisks2/block_devices/sdz1");
const QString kUsbDrive = QStringLiteral("/org/freedesktop/UDisks2/drives/FakeStick");
const QString kDiskBlock = QStringLiteral("/org/freedesktop/UDisks2/block_devices/sdy1");
const QString kDiskDrive = QStringLiteral("/org/freedesktop/UDisks2/drives/FakeDisk");
const QString kLoopBlock = QStringLiteral("/org/freedesktop/UDisks2/block_devices/loop0");

// Point de montage tel que l'envoie UDisks2 : octets terminés par un octet nul.
QByteArray mountPoint(const char *path)
{
    return QByteArray(path) + '\0';
}
}

class TestUsbMonitor : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void detectsRemovableMount();
    void ignoresFixedDisk();
    void ignoresDeviceWithoutDrive();
    void detectsOnlyNewMounts();

private:
    void addObject(const QString &path, QObject *object);
    void sendMountPoints(const QString &block, const QByteArrayList &mountPoints);

    QDBusConnection m_fake{QString()};
    QList<QObject *> m_objects;
};

void TestUsbMonitor::initTestCase()
{
    if (!QDBusConnection::sessionBus().isConnected())
        QSKIP("Pas de bus de session D-Bus : lancer le test avec dbus-run-session.");

    qDBusRegisterMetaType<QByteArrayList>();
    // Connexion séparée pour le faux service, comme un vrai processus distinct.
    m_fake = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("fake-udisks"));
    QVERIFY(m_fake.registerService(QStringLiteral("org.freedesktop.UDisks2")));

    auto *usbBlock = new QObject(this);
    new FakeBlock(usbBlock, kUsbDrive);
    addObject(kUsbBlock, usbBlock);
    auto *usbDrive = new QObject(this);
    new FakeDrive(usbDrive, true);
    addObject(kUsbDrive, usbDrive);

    auto *diskBlock = new QObject(this);
    new FakeBlock(diskBlock, kDiskDrive);
    addObject(kDiskBlock, diskBlock);
    auto *diskDrive = new QObject(this);
    new FakeDrive(diskDrive, false);
    addObject(kDiskDrive, diskDrive);

    auto *loopBlock = new QObject(this);
    new FakeBlock(loopBlock, QStringLiteral("/")); // périphérique loop : pas de Drive
    addObject(kLoopBlock, loopBlock);
}

void TestUsbMonitor::addObject(const QString &path, QObject *object)
{
    QVERIFY(m_fake.registerObject(path, object, QDBusConnection::ExportAdaptors));
}

void TestUsbMonitor::sendMountPoints(const QString &block, const QByteArrayList &mountPoints)
{
    QVariantMap changed;
    changed.insert(QStringLiteral("MountPoints"), QVariant::fromValue(mountPoints));
    QDBusMessage signal = QDBusMessage::createSignal(block, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                     QStringLiteral("PropertiesChanged"));
    signal << QStringLiteral("org.freedesktop.UDisks2.Filesystem") << changed << QStringList();
    QVERIFY(m_fake.send(signal));
}

void TestUsbMonitor::detectsRemovableMount()
{
    UsbMonitor monitor(QDBusConnection::sessionBus());
    QVERIFY(monitor.isAvailable());
    QSignalSpy mounted(&monitor, &UsbMonitor::removableMounted);

    sendMountPoints(kUsbBlock, {mountPoint("/run/media/test/CLE")});

    QVERIFY(mounted.wait(3000));
    QCOMPARE(mounted.first().first().toString(), QStringLiteral("/run/media/test/CLE"));
}

void TestUsbMonitor::ignoresFixedDisk()
{
    UsbMonitor monitor(QDBusConnection::sessionBus());
    QSignalSpy mounted(&monitor, &UsbMonitor::removableMounted);

    sendMountPoints(kDiskBlock, {mountPoint("/mnt/data")});

    QVERIFY(!mounted.wait(500));
}

void TestUsbMonitor::ignoresDeviceWithoutDrive()
{
    UsbMonitor monitor(QDBusConnection::sessionBus());
    QSignalSpy mounted(&monitor, &UsbMonitor::removableMounted);

    sendMountPoints(kLoopBlock, {mountPoint("/run/media/test/ISO")});

    QVERIFY(!mounted.wait(500));
}

void TestUsbMonitor::detectsOnlyNewMounts()
{
    UsbMonitor monitor(QDBusConnection::sessionBus());
    QSignalSpy mounted(&monitor, &UsbMonitor::removableMounted);

    sendMountPoints(kUsbBlock, {mountPoint("/run/media/test/CLE")});
    QVERIFY(mounted.wait(3000));

    // Même point de montage signalé à nouveau : rien de neuf.
    sendMountPoints(kUsbBlock, {mountPoint("/run/media/test/CLE")});
    QVERIFY(!mounted.wait(500));

    // Démontage puis remontage : c'est un nouveau montage.
    sendMountPoints(kUsbBlock, {});
    sendMountPoints(kUsbBlock, {mountPoint("/run/media/test/CLE")});
    QVERIFY(mounted.wait(3000));
    QCOMPARE(mounted.count(), 2);
}

QTEST_GUILESS_MAIN(TestUsbMonitor)
#include "tst_usbmonitor.moc"
