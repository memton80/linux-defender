#include "system/Autostart.h"

#include <QFile>
#include <QStandardPaths>
#include <QTest>

class TestAutostart : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void enableAndDisable();
    void execLine_data();
    void execLine();
};

void TestAutostart::initTestCase()
{
    // Mode test : ~/.qttest/config au lieu de ~/.config, la vraie session n'est pas touchée.
    QStandardPaths::setTestModeEnabled(true);
    QVERIFY(Autostart::desktopFilePath().endsWith(QLatin1String("/autostart/linux-defender.desktop")));
}

void TestAutostart::cleanupTestCase()
{
    Autostart::setEnabled(false);
}

void TestAutostart::enableAndDisable()
{
    QVERIFY(Autostart::setEnabled(false));
    QVERIFY(!Autostart::isEnabled());

    QString error;
    QVERIFY2(Autostart::setEnabled(true, &error), qPrintable(error));
    QVERIFY(Autostart::isEnabled());

    QFile file(Autostart::desktopFilePath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray content = file.readAll();
    QVERIFY(content.startsWith("[Desktop Entry]\n"));
    QVERIFY(content.contains(" --background\n"));

    QVERIFY(Autostart::setEnabled(false));
    QVERIFY(!Autostart::isEnabled());
}

void TestAutostart::execLine_data()
{
    QTest::addColumn<QString>("executable");
    QTest::addColumn<QByteArray>("exec");

    QTest::newRow("commande") << QStringLiteral("linux-defender") << QByteArray("Exec=linux-defender --background");
    QTest::newRow("chemin simple") << QStringLiteral("/usr/bin/linux-defender")
                                   << QByteArray("Exec=/usr/bin/linux-defender --background");
    QTest::newRow("espace") << QStringLiteral("/opt/Mon Appli/linux-defender")
                            << QByteArray("Exec=\"/opt/Mon Appli/linux-defender\" --background");
    // % introduit un code de champ : il est doublé.
    QTest::newRow("pourcent") << QStringLiteral("/opt/100%/linux-defender")
                              << QByteArray("Exec=/opt/100%%/linux-defender --background");
    // $ est protégé par \, lui-même doublé selon le format .desktop.
    QTest::newRow("dollar") << QStringLiteral("/opt/a$b/linux-defender")
                            << QByteArray("Exec=\"/opt/a\\\\$b/linux-defender\" --background");
}

void TestAutostart::execLine()
{
    QFETCH(QString, executable);
    QFETCH(QByteArray, exec);

    const QByteArray content = Autostart::desktopFileContent(executable);
    QVERIFY2(content.contains(exec + '\n'), content.constData());
}

QTEST_GUILESS_MAIN(TestAutostart)
#include "tst_autostart.moc"
