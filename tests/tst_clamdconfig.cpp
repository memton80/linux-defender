#include "core/ClamdConfig.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace
{
void writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}
}

class TestClamdConfig : public QObject
{
    Q_OBJECT

private slots:
    void parseSize_data();
    void parseSize();
    void defaults();
    void parsesDirectives();
    void unscannedAbove();
    void findsConfigOfSocket();
};

void TestClamdConfig::parseSize_data()
{
    QTest::addColumn<QString>("value");
    QTest::addColumn<qint64>("bytes");

    // Suffixes acceptés par clamd (voir clamconf -g clamd.conf).
    QTest::newRow("octets") << "1048576" << qint64(1048576);
    QTest::newRow("K") << "512K" << qint64(512 * 1024);
    QTest::newRow("k") << "512k" << qint64(512 * 1024);
    QTest::newRow("M") << "100M" << qint64(100) * 1024 * 1024;
    QTest::newRow("m") << "400m" << qint64(400) * 1024 * 1024;
    QTest::newRow("G") << "2G" << qint64(2) * 1024 * 1024 * 1024;
    QTest::newRow("zéro") << "0" << qint64(0);
    QTest::newRow("espaces") << " 25M " << qint64(25) * 1024 * 1024;
    QTest::newRow("invalide") << "cent" << qint64(-1);
    QTest::newRow("unité inconnue") << "10T" << qint64(-1);
    QTest::newRow("vide") << "" << qint64(-1);
}

void TestClamdConfig::parseSize()
{
    QFETCH(QString, value);
    QFETCH(qint64, bytes);
    QCOMPARE(ClamdConfig::parseSize(value), bytes);
}

void TestClamdConfig::defaults()
{
    // Valeurs par défaut de ClamAV 1.x (clamconf 1.5.4).
    const ClamdConfig config = ClamdConfig::parse(QString());
    QCOMPARE(config.maxFileSize, qint64(100) * 1024 * 1024);
    QCOMPARE(config.maxScanSize, qint64(400) * 1024 * 1024);
    QVERIFY(!config.alertExceedsMax);
    QVERIFY(!config.exampleLine);
    QVERIFY(config.localSocket.isEmpty());
    QVERIFY(config.path.isEmpty());
}

void TestClamdConfig::parsesDirectives()
{
    // Extrait d'un scan.conf de Fedora, lignes commentées comprises.
    const ClamdConfig config = ClamdConfig::parse(QStringLiteral(
        "# Comment or remove the line below.\n"
        "Example\n"
        "#LocalSocket /run/clamd.scan/clamd.sock\n"
        "LocalSocket /run/clamav/clamd.ctl\n"
        "  MaxFileSize 25M\n"
        "#MaxScanSize 1G\n"
        "MaxScanSize 200M\n"
        "AlertExceedsMax yes\n"));
    QVERIFY(config.exampleLine);
    QCOMPARE(config.localSocket, QStringLiteral("/run/clamav/clamd.ctl"));
    QCOMPARE(config.maxFileSize, qint64(25) * 1024 * 1024);
    QCOMPARE(config.maxScanSize, qint64(200) * 1024 * 1024);
    QVERIFY(config.alertExceedsMax);

    QVERIFY(!ClamdConfig::parse(QStringLiteral("#Example\nAlertExceedsMax no\n")).exampleLine);
    QVERIFY(!ClamdConfig::parse(QStringLiteral("AlertExceedsMax no\n")).alertExceedsMax);
    QVERIFY(ClamdConfig::parse(QStringLiteral("AlertExceedsMax true\n")).alertExceedsMax);
    // Valeur illisible : la valeur par défaut reste.
    QCOMPARE(ClamdConfig::parse(QStringLiteral("MaxFileSize beaucoup\n")).maxFileSize,
             ClamdConfig::kDefaultMaxFileSize);
}

void TestClamdConfig::unscannedAbove()
{
    ClamdConfig config;
    QCOMPARE(config.unscannedAbove(), ClamdConfig::kDefaultMaxFileSize);
    config.maxFileSize = 1024;
    QCOMPARE(config.unscannedAbove(), qint64(1024));
    // 0 : pas de limite dans la configuration, mais le moteur en a une (2 Go).
    config.maxFileSize = 0;
    QCOMPARE(config.unscannedAbove(), ClamdConfig::kEngineMaxFileSize);
    config.maxFileSize = qint64(4) * 1024 * 1024 * 1024;
    QCOMPARE(config.unscannedAbove(), ClamdConfig::kEngineMaxFileSize);
}

void TestClamdConfig::findsConfigOfSocket()
{
    QTemporaryDir dir;
    const QString fedora = dir.filePath(QStringLiteral("scan.conf"));
    const QString debian = dir.filePath(QStringLiteral("clamd.conf"));
    const QString missing = dir.filePath(QStringLiteral("absent.conf"));
    writeFile(fedora, "LocalSocket /run/clamd.scan/clamd.sock\nMaxFileSize 10M\n");
    writeFile(debian, "LocalSocket /run/clamav/clamd.ctl\nMaxFileSize 20M\n");
    const QStringList files{missing, fedora, debian};

    // Le fichier qui déclare le socket utilisé.
    ClamdConfig config = ClamdConfig::forSocket(QStringLiteral("/run/clamav/clamd.ctl"), files);
    QCOMPARE(config.path, debian);
    QCOMPARE(config.maxFileSize, qint64(20) * 1024 * 1024);
    config = ClamdConfig::forSocket(QStringLiteral("/run/clamd.scan//clamd.sock"), files);
    QCOMPARE(config.path, fedora);

    // Socket d'aucun fichier (choisi dans les paramètres) : le premier lisible.
    config = ClamdConfig::forSocket(QStringLiteral("/tmp/autre.sock"), files);
    QCOMPARE(config.path, fedora);

    // Socket désigné par un autre chemin (lien symbolique, comme /var/run -> /run).
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("run/clamav"))));
    QVERIFY(QFile::link(dir.filePath(QStringLiteral("run")), dir.filePath(QStringLiteral("var-run"))));
    writeFile(dir.filePath(QStringLiteral("run/clamav/clamd.ctl")), "");
    const QString linked = dir.filePath(QStringLiteral("linked.conf"));
    writeFile(linked, "LocalSocket " + QFile::encodeName(dir.filePath(QStringLiteral("var-run/clamav/clamd.ctl"))) + '\n');
    config = ClamdConfig::forSocket(dir.filePath(QStringLiteral("run/clamav/clamd.ctl")), {fedora, linked});
    QCOMPARE(config.path, linked);

    // Aucun fichier lisible : valeurs par défaut.
    config = ClamdConfig::forSocket(QStringLiteral("/run/clamav/clamd.ctl"), {missing});
    QVERIFY(config.path.isEmpty());
    QCOMPARE(config.maxFileSize, ClamdConfig::kDefaultMaxFileSize);
}

QTEST_GUILESS_MAIN(TestClamdConfig)
#include "tst_clamdconfig.moc"
