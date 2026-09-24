#include "core/Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>

class TestSettings : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void defaults();
    void defaultQuickScanPaths();
    void quickScanPathsFallBackToDefaults();
    void scanOptions();
    void boundsInvalidValues();
};

void TestSettings::initTestCase()
{
    // Mode test : ~/.qttest/config au lieu de ~/.config, les vrais réglages ne sont pas touchés.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("linux-defender-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_settings"));
}

void TestSettings::init()
{
    QSettings().clear();
}

void TestSettings::defaults()
{
    QCOMPARE(Settings::socketPath(), QString());
    QCOMPARE(Settings::checkInterval(), Settings::Defaults::checkInterval);
    QCOMPARE(Settings::signaturesMaxAge(), Settings::Defaults::signaturesMaxAge);
    QCOMPARE(Settings::excludedPaths(), QStringList());
    QCOMPARE(Settings::scanHidden(), Settings::Defaults::scanHidden);
    QCOMPARE(Settings::maxFileSizeMb(), Settings::Defaults::maxFileSizeMb);
    QCOMPARE(Settings::usbAutoScan(), Settings::Defaults::usbAutoScan);
    QCOMPARE(Settings::usbNotify(), Settings::Defaults::usbNotify);
    QCOMPARE(Settings::notifyScanFinished(), Settings::Defaults::notifyScanFinished);
    QCOMPARE(Settings::notifyRealtime(), Settings::Defaults::notifyRealtime);
    QCOMPARE(Settings::notifyClamdLost(), Settings::Defaults::notifyClamdLost);
    QCOMPARE(Settings::notifySignatures(), Settings::Defaults::notifySignatures);
    QCOMPARE(Settings::closeToTray(), Settings::Defaults::closeToTray);
    QCOMPARE(Settings::historyMaxEntries(), Settings::Defaults::historyMaxEntries);
}

void TestSettings::defaultQuickScanPaths()
{
    const QStringList paths = Settings::Defaults::quickScanPaths();
    QVERIFY(!paths.isEmpty());
    // Jamais le dossier personnel lui-même (ce serait une analyse complète),
    // sauf s'il n'existe aucun des dossiers usuels.
    if (paths != QStringList{QDir::homePath()}) {
        for (const QString &path : paths) {
            QVERIFY2(QDir::cleanPath(path) != QDir::cleanPath(QDir::homePath()), qPrintable(path));
            QVERIFY2(QFileInfo(path).isDir(), qPrintable(path));
        }
    }
    QCOMPARE(paths.size(), QSet<QString>(paths.begin(), paths.end()).size());
}

void TestSettings::quickScanPathsFallBackToDefaults()
{
    QCOMPARE(Settings::quickScanPaths(), Settings::Defaults::quickScanPaths());

    Settings::setQuickScanPaths({QStringLiteral("/srv/partage"), QString()});
    QCOMPARE(Settings::quickScanPaths(), QStringList{QStringLiteral("/srv/partage")});

    Settings::setQuickScanPaths({});
    QCOMPARE(Settings::quickScanPaths(), Settings::Defaults::quickScanPaths());
}

void TestSettings::scanOptions()
{
    Settings::setExcludedPaths({QStringLiteral("/home/alex/VM"), QStringLiteral("/home/alex/faux.exe")});
    Settings::setScanHidden(false);
    Settings::setMaxFileSizeMb(100);

    const ScanOptions options = Settings::scanOptions();
    QCOMPARE(options.excludedPaths, Settings::excludedPaths());
    QCOMPARE(options.scanHidden, false);
    QCOMPARE(options.maxFileSize, qint64(100) * 1024 * 1024);

    Settings::setMaxFileSizeMb(0);
    QCOMPARE(Settings::scanOptions().maxFileSize, qint64(0));
}

void TestSettings::boundsInvalidValues()
{
    // Fichier de réglages modifié à la main : valeurs ramenées dans leurs bornes.
    QSettings settings;
    settings.setValue(QStringLiteral("clamd/checkInterval"), 0);
    settings.setValue(QStringLiteral("clamd/signaturesMaxAge"), -4);
    settings.setValue(QStringLiteral("history/maxEntries"), QStringLiteral("beaucoup"));
    settings.sync();

    QCOMPARE(Settings::checkInterval(), 5);
    QCOMPARE(Settings::signaturesMaxAge(), 0);
    QCOMPARE(Settings::historyMaxEntries(), Settings::Defaults::historyMaxEntries);
}

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
