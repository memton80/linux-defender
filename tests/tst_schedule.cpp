#include "FakeClamd.h"
#include "core/ClamdClient.h"
#include "core/ClamdWatcher.h"
#include "core/ScanManager.h"
#include "core/Settings.h"
#include "system/ScanSchedule.h"

#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class TestSchedule : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void isDue_data();
    void isDue();
    void nextRun();
    void launchesWhenDue();
    void waitsForClamdAndRunningScans();
    void failedScanStaysDue();

private:
    QString socketPath() const { return m_socketDir.filePath(QStringLiteral("clamd.sock")); }

    QTemporaryDir m_socketDir;
    QTemporaryDir m_files;
};

void TestSchedule::initTestCase()
{
    // Réglages dans un dossier de test, pas dans ceux de l'utilisateur.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("linux-defender-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_schedule"));
}

void TestSchedule::init()
{
    QSettings().clear();
}

void TestSchedule::isDue_data()
{
    QTest::addColumn<QDateTime>("lastRun");
    QTest::addColumn<int>("frequency");
    QTest::addColumn<bool>("due");

    const QDateTime now(QDate(2026, 9, 25), QTime(9, 0));
    const int never = int(ScanSchedule::Frequency::Never);
    const int daily = int(ScanSchedule::Frequency::Daily);
    const int weekly = int(ScanSchedule::Frequency::Weekly);
    QTest::newRow("jamais planifiée") << QDateTime() << never << false;
    QTest::newRow("jamais faite") << QDateTime() << daily << true;
    QTest::newRow("hier même heure") << now.addDays(-1) << daily << true;
    // Vérifications tous les quarts d'heure : une heure de marge, sans dérive.
    QTest::newRow("il y a 23 h") << now.addSecs(-23 * 3600) << daily << true;
    QTest::newRow("il y a 22 h") << now.addSecs(-22 * 3600) << daily << false;
    QTest::newRow("hier soir") << QDateTime(QDate(2026, 9, 24), QTime(23, 50)) << daily << false;
    QTest::newRow("semaine : 6 jours") << now.addDays(-6) << weekly << false;
    QTest::newRow("semaine : 7 jours moins 1 h") << now.addSecs(-(7 * 24 - 1) * 3600) << weekly << true;
    QTest::newRow("horloge qui a reculé") << now.addDays(2) << weekly << true;
}

void TestSchedule::isDue()
{
    QFETCH(QDateTime, lastRun);
    QFETCH(int, frequency);
    QFETCH(bool, due);
    const QDateTime now(QDate(2026, 9, 25), QTime(9, 0));
    QCOMPARE(ScanSchedule::isDue(lastRun, now, ScanSchedule::Frequency(frequency)), due);
}

void TestSchedule::nextRun()
{
    const QDateTime now(QDate(2026, 9, 25), QTime(9, 0));
    const QDateTime last = now.addSecs(-3600);
    QVERIFY(!ScanSchedule::nextRun(last, now, ScanSchedule::Frequency::Never).isValid());
    QCOMPARE(ScanSchedule::nextRun(last, now, ScanSchedule::Frequency::Daily), last.addSecs(23 * 3600));
    QCOMPARE(ScanSchedule::nextRun(QDateTime(), now, ScanSchedule::Frequency::Weekly), now); // dès que possible
}

void TestSchedule::launchesWhenDue()
{
    // Le faux clamd répond pareil à toutes les commandes : un pour VERSION
    // (état de clamd), un autre pour PING et FILDES (analyse).
    const QString versionSocket = m_socketDir.filePath(QStringLiteral("version.sock"));
    FakeClamd versionClamd(versionSocket, QByteArrayLiteral("ClamAV 1.5.4/27775/Thu Sep 24 08:26:12 2026\0"));
    FakeClamd scanClamd(socketPath(), QByteArrayLiteral("PONG\0"));
    ClamdClient watcherClient;
    watcherClient.setSocketPath(versionSocket);
    ClamdWatcher watcher(&watcherClient);
    QSignalSpy checked(&watcher, &ClamdWatcher::checkFinished);
    watcher.checkNow();
    QVERIFY(checked.wait(5000));
    QCOMPARE(watcher.state(), ClamdWatcher::State::Connected);

    ClamdClient client;
    client.setSocketPath(socketPath());
    ScanManager scans(&client);
    scans.setQuickScan({m_files.path()}, false);
    // Bus non connecté : pas d'UPower, l'état de la batterie n'est pas consulté.
    ScanSchedule schedule(&scans, &watcher, QDBusConnection(QStringLiteral("absent")));
    QSignalSpy launched(&schedule, &ScanSchedule::scanLaunched);
    QSignalSpy finished(&scans, &ScanManager::scanFinished);

    // Pas planifiée : rien.
    schedule.checkNow();
    QCOMPARE(launched.count(), 0);

    schedule.setSchedule(ScanSchedule::Frequency::Daily, ScanSchedule::Kind::Quick, true);
    schedule.checkNow();
    QCOMPARE(launched.count(), 1);
    QCOMPARE(launched.first().first().value<ScanSchedule::Kind>(), ScanSchedule::Kind::Quick);
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.first().at(1).value<ScanManager::Origin>(), ScanManager::Origin::Scheduled);
    QVERIFY(finished.first().first().value<ScanSummary>().fatalError.isEmpty());

    // Faite : enregistrée, plus due avant demain.
    QVERIFY(schedule.lastRun().isValid());
    QVERIFY(qAbs(schedule.lastRun().secsTo(QDateTime::currentDateTime())) < 5);
    QCOMPARE(Settings::scheduleLastRun(), schedule.lastRun());
    schedule.checkNow();
    QCOMPARE(launched.count(), 1);
    QVERIFY(schedule.nextRun() > QDateTime::currentDateTime().addSecs(22 * 3600));
}

void TestSchedule::waitsForClamdAndRunningScans()
{
    // clamd injoignable : l'analyse attend (elle échouerait aussitôt).
    ClamdClient client;
    client.setSocketPath(socketPath()); // aucun faux clamd n'écoute
    ClamdWatcher watcher(&client);
    ScanManager scans(&client);
    ScanSchedule schedule(&scans, &watcher, QDBusConnection(QStringLiteral("absent")));
    schedule.setSchedule(ScanSchedule::Frequency::Weekly, ScanSchedule::Kind::Full, false);
    QSignalSpy launched(&schedule, &ScanSchedule::scanLaunched);
    schedule.checkNow();
    QCOMPARE(launched.count(), 0);
    QVERIFY(!scans.isScanning());
}

void TestSchedule::failedScanStaysDue()
{
    // Analyse planifiée qui échoue (clamd perdu) : toujours due.
    ClamdClient client;
    client.setSocketPath(socketPath());
    ClamdWatcher watcher(&client);
    ScanManager scans(&client);
    ScanSchedule schedule(&scans, &watcher, QDBusConnection(QStringLiteral("absent")));
    schedule.setSchedule(ScanSchedule::Frequency::Daily, ScanSchedule::Kind::Quick, false);
    QSignalSpy finished(&scans, &ScanManager::scanFinished);
    scans.scan({m_files.path()}, ScanManager::Origin::Scheduled);
    QVERIFY(finished.wait(10000));
    QVERIFY(!finished.first().first().value<ScanSummary>().fatalError.isEmpty());
    QVERIFY(!schedule.lastRun().isValid());
}

QTEST_GUILESS_MAIN(TestSchedule)
#include "tst_schedule.moc"
