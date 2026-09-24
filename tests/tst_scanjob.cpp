#include "FakeClamd.h"
#include "core/ClamdClient.h"
#include "core/ScanJob.h"
#include "core/ScanManager.h"

#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <memory>

#include <sys/stat.h>
#include <unistd.h>

namespace
{
// Tout ce qu'un scan a émis.
struct Run
{
    bool finished = false;
    ScanSummary summary;
    QList<ScanResult> results;
    qint64 lastCount = -1;
    qint64 lastDone = -1;
    qint64 lastTotal = -1;
};

// Lance le scan et attend sa fin. ScanJob émet depuis son thread : les
// signaux arrivent ici par la file d'événements, traitée par `loop`.
Run runJob(ScanJob &job, bool cancelAtOnce = false)
{
    Run run;
    QEventLoop loop;
    QObject::connect(&job, &ScanJob::counting, &loop, [&](qint64 found) { run.lastCount = found; });
    QObject::connect(&job, &ScanJob::progressChanged, &loop, [&](qint64 done, qint64 total) {
        run.lastDone = done;
        run.lastTotal = total;
    });
    QObject::connect(&job, &ScanJob::resultsReady, &loop, [&](const QList<ScanResult> &results) { run.results += results; });
    QObject::connect(&job, &ScanJob::finished, &loop, [&](const ScanSummary &summary) {
        run.summary = summary;
        run.finished = true;
        loop.quit();
    });
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    job.start();
    if (cancelAtOnce)
        job.cancel();
    loop.exec();
    return run;
}

ScanResult find(const Run &run, const QString &path)
{
    for (const ScanResult &result : run.results) {
        if (result.path == path)
            return result;
    }
    return {QStringLiteral("<absent>"), ScanResult::Status::Clean, {}};
}

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

bool isRoot()
{
    return ::geteuid() == 0;
}
}

class TestScanJob : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void scansFolderRecursively();
    void scansSingleFile();
    void reportsMissingPath();
    void reportsUnreadableFile();
    void reportsUnreadableFolder();
    void failsWhenClamdMissing();
    void canBeCancelled();
    void appliesExclusions();
    void canSkipHiddenFiles();
    void skipsLargeFiles();
    void managerQueuesScans();
    void managerAppliesOptions();
    void parseReply_data();
    void parseReply();

private:
    QString socketPath() const { return m_socketDir.filePath(QStringLiteral("clamd.sock")); }

    QTemporaryDir m_socketDir;
    QString m_root; // dossier à scanner, chemin canonique
    std::unique_ptr<QTemporaryDir> m_files;
};

void TestScanJob::init()
{
    m_files = std::make_unique<QTemporaryDir>();
    m_root = QFileInfo(m_files->path()).canonicalFilePath();
}

void TestScanJob::scansFolderRecursively()
{
    FakeClamd clamd(socketPath(), QByteArrayLiteral("PONG\0"));
    writeFile(m_root + QStringLiteral("/sain.txt"), "bonjour");
    writeFile(m_root + QStringLiteral("/sous/dossier/virus.bin"), QByteArray("xx") + FakeClamd::kVirusMarker);
    writeFile(m_root + QStringLiteral("/sous/erreur.txt"), FakeClamd::kErrorMarker);
    writeFile(m_root + QStringLiteral("/.cache/cache.txt"), "fichier caché");
    // Ignorés : lien symbolique et FIFO (ouvrir une FIFO bloquerait).
    QVERIFY(QFile::link(m_root + QStringLiteral("/sain.txt"), m_root + QStringLiteral("/lien.txt")));
    QCOMPARE(::mkfifo(QFile::encodeName(m_root + QStringLiteral("/fifo")).constData(), 0600), 0);

    ScanJob job(socketPath(), {m_root});
    const Run run = runJob(job);

    QVERIFY(run.finished);
    QVERIFY2(run.summary.fatalError.isEmpty(), qPrintable(run.summary.fatalError));
    QVERIFY(!run.summary.cancelled);
    QCOMPARE(run.summary.scanned, qint64(4));
    QCOMPARE(run.summary.infected, qint64(1));
    QCOMPARE(run.summary.errors, qint64(1));
    QCOMPARE(run.results.size(), 4);
    QCOMPARE(clamd.filesScanned(), 4);

    const ScanResult virus = find(run, m_root + QStringLiteral("/sous/dossier/virus.bin"));
    QCOMPARE(int(virus.status), int(ScanResult::Status::Infected));
    QCOMPARE(virus.detail, QString::fromLatin1(FakeClamd::kVirusName));
    QCOMPARE(int(find(run, m_root + QStringLiteral("/sous/erreur.txt")).status), int(ScanResult::Status::Error));
    QCOMPARE(int(find(run, m_root + QStringLiteral("/.cache/cache.txt")).status), int(ScanResult::Status::Clean));

    QCOMPARE(run.lastCount, qint64(4));
    QCOMPARE(run.lastDone, qint64(4));
    QCOMPARE(run.lastTotal, qint64(4));

    // Bilan : menaces citées, début et durée du scan.
    QCOMPARE(run.summary.threats.size(), 1);
    QCOMPARE(run.summary.threats.first().path, virus.path);
    QCOMPARE(run.summary.threats.first().detail, virus.detail);
    QVERIFY(run.summary.started.isValid());
    QVERIFY(run.summary.started <= QDateTime::currentDateTime());
    QVERIFY(run.summary.elapsedMsecs >= 0);
    QCOMPARE(run.summary.skipped, qint64(0));
}

void TestScanJob::scansSingleFile()
{
    FakeClamd clamd(socketPath(), QByteArrayLiteral("PONG\0"));
    const QString file = m_root + QStringLiteral("/virus.bin");
    writeFile(file, FakeClamd::kVirusMarker);

    ScanJob job(socketPath(), {file});
    const Run run = runJob(job);

    QCOMPARE(run.summary.scanned, qint64(1));
    QCOMPARE(run.summary.infected, qint64(1));
    QCOMPARE(run.results.size(), 1);
    QCOMPARE(run.results.first().path, file);
    // PING d'abord, puis une commande FILDES par fichier.
    QCOMPARE(clamd.received(), QByteArrayLiteral("zPING\0zFILDES\0"));
}

void TestScanJob::reportsMissingPath()
{
    FakeClamd clamd(socketPath(), QByteArrayLiteral("PONG\0"));
    const QString missing = m_root + QStringLiteral("/absent");

    ScanJob job(socketPath(), {missing});
    const Run run = runJob(job);

    QCOMPARE(run.summary.scanned, qint64(0));
    QCOMPARE(run.summary.errors, qint64(1));
    QCOMPARE(run.results.size(), 1);
    QCOMPARE(int(run.results.first().status), int(ScanResult::Status::Error));
}

void TestScanJob::reportsUnreadableFile()
{
    if (isRoot())
        QSKIP("root ignore les permissions des fichiers");
    FakeClamd clamd(socketPath(), QByteArrayLiteral("PONG\0"));
    const QString file = m_root + QStringLiteral("/secret.txt");
    writeFile(file, "secret");
    QVERIFY(QFile::setPermissions(file, QFileDevice::Permissions()));

    ScanJob job(socketPath(), {m_root});
    const Run run = runJob(job);

    QCOMPARE(run.summary.scanned, qint64(1));
    QCOMPARE(run.summary.errors, qint64(1));
    const ScanResult result = find(run, file);
    QCOMPARE(int(result.status), int(ScanResult::Status::Error));
    QVERIFY2(result.detail.startsWith(QLatin1String("Ouverture impossible")), qPrintable(result.detail));
    QCOMPARE(clamd.filesScanned(), 0);
}

void TestScanJob::reportsUnreadableFolder()
{
    if (isRoot())
        QSKIP("root ignore les permissions des fichiers");
    FakeClamd clamd(socketPath(), QByteArrayLiteral("PONG\0"));
    const QString folder = m_root + QStringLiteral("/prive");
    writeFile(folder + QStringLiteral("/fichier.txt"), "x");
    writeFile(m_root + QStringLiteral("/public.txt"), "x");
    QVERIFY(QFile::setPermissions(folder, QFileDevice::Permissions()));

    ScanJob job(socketPath(), {m_root});
    const Run run = runJob(job);
    QFile::setPermissions(folder, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    QCOMPARE(run.summary.scanned, qint64(1)); // public.txt
    QCOMPARE(run.summary.errors, qint64(1));  // le dossier
    const ScanResult result = find(run, folder);
    QCOMPARE(int(result.status), int(ScanResult::Status::Error));
    QVERIFY2(result.detail.startsWith(QLatin1String("Dossier illisible")), qPrintable(result.detail));
}

void TestScanJob::failsWhenClamdMissing()
{
    writeFile(m_root + QStringLiteral("/fichier.txt"), "x");

    ScanJob job(socketPath(), {m_root});
    const Run run = runJob(job);

    QVERIFY(run.finished);
    QVERIFY(run.summary.fatalError.contains(QLatin1String("introuvable")));
    QCOMPARE(run.summary.scanned, qint64(0));
    QVERIFY(run.results.isEmpty());
}

void TestScanJob::canBeCancelled()
{
    FakeClamd clamd(socketPath(), QByteArrayLiteral("PONG\0"));
    for (int i = 0; i < 200; ++i)
        writeFile(m_root + QStringLiteral("/f%1.txt").arg(i), "x");

    ScanJob job(socketPath(), {m_root});
    const Run run = runJob(job, true);

    QVERIFY(run.finished);
    QVERIFY(run.summary.cancelled);
    QVERIFY(run.summary.scanned < 200);
}

void TestScanJob::appliesExclusions()
{
    FakeClamd clamd(socketPath(), QByteArrayLiteral("PONG\0"));
    writeFile(m_root + QStringLiteral("/garde.txt"), "x");
    writeFile(m_root + QStringLiteral("/vm/disque.img"), FakeClamd::kVirusMarker);
    writeFile(m_root + QStringLiteral("/vm/sous/autre.img"), "x");
    writeFile(m_root + QStringLiteral("/faux-positif.exe"), FakeClamd::kVirusMarker);
    // Même début de nom qu'un dossier exclu, mais pas dedans : analysé.
    writeFile(m_root + QStringLiteral("/vm2/garde.txt"), "x");

    ScanOptions options;
    // Chemin non canonique (« // », « /./ ») : rendu canonique par le scan.
    options.excludedPaths = {m_root + QStringLiteral("//vm/"), m_root + QStringLiteral("/./faux-positif.exe"),
                             QStringLiteral("/chemin/qui/n-existe/pas")};
    ScanJob job(socketPath(), {m_root}, options);
    const Run run = runJob(job);

    QVERIFY2(run.summary.fatalError.isEmpty(), qPrintable(run.summary.fatalError));
    QCOMPARE(run.summary.scanned, qint64(2));
    QCOMPARE(run.summary.infected, qint64(0));
    QCOMPARE(run.lastTotal, qint64(2)); // les exclusions ne sont pas comptées non plus
    QCOMPARE(find(run, m_root + QStringLiteral("/vm/disque.img")).path, QStringLiteral("<absent>"));
    QCOMPARE(int(find(run, m_root + QStringLiteral("/vm2/garde.txt")).status), int(ScanResult::Status::Clean));

    // Choisi explicitement, un élément exclu est quand même analysé.
    ScanJob explicitJob(socketPath(), {m_root + QStringLiteral("/vm")}, options);
    const Run explicitRun = runJob(explicitJob);
    QCOMPARE(explicitRun.summary.scanned, qint64(2));
    QCOMPARE(explicitRun.summary.infected, qint64(1));
}

void TestScanJob::canSkipHiddenFiles()
{
    FakeClamd clamd(socketPath(), QByteArrayLiteral("PONG\0"));
    writeFile(m_root + QStringLiteral("/visible.txt"), "x");
    writeFile(m_root + QStringLiteral("/.cache/cache.txt"), "x");
    writeFile(m_root + QStringLiteral("/.cache.txt"), "x");

    ScanOptions options;
    options.scanHidden = false;
    ScanJob job(socketPath(), {m_root}, options);
    const Run run = runJob(job);

    QCOMPARE(run.summary.scanned, qint64(1));
    QCOMPARE(run.results.size(), 1);
    QCOMPARE(run.results.first().path, m_root + QStringLiteral("/visible.txt"));
}

void TestScanJob::skipsLargeFiles()
{
    FakeClamd clamd(socketPath(), QByteArrayLiteral("PONG\0"));
    const QString big = m_root + QStringLiteral("/gros.iso");
    writeFile(m_root + QStringLiteral("/petit.txt"), "x");
    writeFile(big, QByteArray(100, 'x') + FakeClamd::kVirusMarker);

    ScanOptions options;
    options.maxFileSize = 50;
    ScanJob job(socketPath(), {m_root}, options);
    const Run run = runJob(job);

    QCOMPARE(run.summary.scanned, qint64(1));
    QCOMPARE(run.summary.skipped, qint64(1));
    QCOMPARE(run.summary.infected, qint64(0));
    // Progression cohérente : le fichier ignoré n'est pas dans le total.
    QCOMPARE(run.lastDone, run.lastTotal);

    // Choisi explicitement, un gros fichier est quand même analysé.
    ScanJob explicitJob(socketPath(), {big}, options);
    const Run explicitRun = runJob(explicitJob);
    QCOMPARE(explicitRun.summary.skipped, qint64(0));
    QCOMPARE(explicitRun.summary.infected, qint64(1));
}

void TestScanJob::managerQueuesScans()
{
    FakeClamd clamd(socketPath(), QByteArrayLiteral("PONG\0"));
    const QString first = m_root + QStringLiteral("/premier.txt");
    const QString second = m_root + QStringLiteral("/second.txt");
    writeFile(first, "x");
    writeFile(second, FakeClamd::kVirusMarker);

    ClamdClient client;
    client.setSocketPath(socketPath());
    ScanManager manager(&client);
    QSignalSpy started(&manager, &ScanManager::scanStarted);
    QSignalSpy finished(&manager, &ScanManager::scanFinished);

    manager.scan({first}, ScanManager::Origin::Manual);
    manager.scan({second}, ScanManager::Origin::Usb);   // en file d'attente
    manager.scan({second}, ScanManager::Origin::Usb);   // doublon : ignoré
    QVERIFY(manager.isScanning());

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 10000);
    QCOMPARE(started.count(), 2);
    QCOMPARE(started.at(0).at(0).toStringList(), QStringList{first});
    QCOMPARE(started.at(1).at(0).toStringList(), QStringList{second});
    QCOMPARE(finished.at(1).at(1).value<ScanManager::Origin>(), ScanManager::Origin::Usb);
    QCOMPARE(finished.at(1).at(0).value<ScanSummary>().infected, qint64(1));
    QVERIFY(!manager.isScanning());
}

void TestScanJob::managerAppliesOptions()
{
    FakeClamd clamd(socketPath(), QByteArrayLiteral("PONG\0"));
    writeFile(m_root + QStringLiteral("/garde.txt"), "x");
    writeFile(m_root + QStringLiteral("/exclu/virus.bin"), FakeClamd::kVirusMarker);

    ClamdClient client;
    client.setSocketPath(socketPath());
    ScanManager manager(&client);
    ScanOptions options;
    options.excludedPaths = {m_root + QStringLiteral("/exclu")};
    manager.setOptions(options);
    QCOMPARE(manager.options(), options);
    QSignalSpy finished(&manager, &ScanManager::scanFinished);

    manager.scan({m_root}, ScanManager::Origin::Full);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    QCOMPARE(finished.at(0).at(1).value<ScanManager::Origin>(), ScanManager::Origin::Full);
    const ScanSummary summary = finished.at(0).at(0).value<ScanSummary>();
    QCOMPARE(summary.scanned, qint64(1));
    QCOMPARE(summary.infected, qint64(0));
}

void TestScanJob::parseReply_data()
{
    QTest::addColumn<QByteArray>("reply");
    QTest::addColumn<bool>("recognized");
    QTest::addColumn<int>("status");
    QTest::addColumn<QString>("detail");

    const int clean = int(ScanResult::Status::Clean);
    const int infected = int(ScanResult::Status::Infected);
    const int error = int(ScanResult::Status::Error);

    // Formats relevés sur un vrai clamd 1.5.
    QTest::newRow("sain") << QByteArray("fd[10]: OK") << true << clean << QString();
    QTest::newRow("menace") << QByteArray("fd[10]: Win.Test.EICAR_HDB-1 FOUND") << true << infected
                            << QStringLiteral("Win.Test.EICAR_HDB-1");
    QTest::newRow("signature non officielle") << QByteArray("fd[12]: Local.Test.UNOFFICIAL FOUND") << true
                                              << infected << QStringLiteral("Local.Test.UNOFFICIAL");
    QTest::newRow("erreur") << QByteArray("fd[10]: Can't allocate memory ERROR") << true << error
                            << QStringLiteral("Erreur de clamd : Can't allocate memory");
    QTest::newRow("erreur sans préfixe") << QByteArray("No file descriptor received. ERROR") << true << error
                                         << QStringLiteral("Erreur de clamd : No file descriptor received.");
    QTest::newRow("commande inconnue") << QByteArray("UNKNOWN COMMAND") << false << 0 << QString();
    QTest::newRow("vide") << QByteArray() << false << 0 << QString();
}

void TestScanJob::parseReply()
{
    QFETCH(QByteArray, reply);
    QFETCH(bool, recognized);
    QFETCH(int, status);
    QFETCH(QString, detail);

    const std::optional<ScanResult> result = ScanJob::parseReply(QStringLiteral("/f"), reply);
    QCOMPARE(result.has_value(), recognized);
    if (result) {
        QCOMPARE(int(result->status), status);
        QCOMPARE(result->detail, detail);
        QCOMPARE(result->path, QStringLiteral("/f"));
    }
}

QTEST_GUILESS_MAIN(TestScanJob)
#include "tst_scanjob.moc"
