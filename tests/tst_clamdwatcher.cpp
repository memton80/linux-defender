#include "FakeClamd.h"
#include "core/ClamdWatcher.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
const QByteArray kVersionReply = QByteArrayLiteral("ClamAV 1.4.2/27400/Tue Sep 23 08:26:12 2025\0");
}

class TestClamdWatcher : public QObject
{
    Q_OBJECT

private slots:
    void unknownBeforeFirstCheck();
    void connectedWhenClamdAnswers();
    void errorWhenClamdMissing();
    void recoversWhenClamdStarts();
    void statusChangedOnlyOnChange();

private:
    QString socketPath() const { return m_dir.filePath(QString::fromLatin1(QTest::currentTestFunction())); }

    QTemporaryDir m_dir;
};

void TestClamdWatcher::unknownBeforeFirstCheck()
{
    ClamdClient client;
    ClamdWatcher watcher(&client);
    QCOMPARE(watcher.state(), ClamdWatcher::State::Unknown);
    QVERIFY(!watcher.lastCheck().isValid());
}

void TestClamdWatcher::connectedWhenClamdAnswers()
{
    FakeClamd clamd(socketPath(), kVersionReply);
    ClamdClient client;
    client.setSocketPath(socketPath());
    ClamdWatcher watcher(&client);

    QSignalSpy changed(&watcher, &ClamdWatcher::statusChanged);
    watcher.start();

    QVERIFY(changed.wait(2000));
    QCOMPARE(watcher.state(), ClamdWatcher::State::Connected);
    QCOMPARE(watcher.version().engine, QStringLiteral("1.4.2"));
    QVERIFY(watcher.errorMessage().isEmpty());
    QVERIFY(watcher.lastCheck().isValid());
}

void TestClamdWatcher::errorWhenClamdMissing()
{
    ClamdClient client;
    client.setSocketPath(socketPath());
    ClamdWatcher watcher(&client);

    QSignalSpy changed(&watcher, &ClamdWatcher::statusChanged);
    watcher.start();

    QVERIFY(changed.wait(2000));
    QCOMPARE(watcher.state(), ClamdWatcher::State::Error);
    QVERIFY(watcher.errorMessage().contains(socketPath()));
}

void TestClamdWatcher::recoversWhenClamdStarts()
{
    ClamdClient client;
    client.setSocketPath(socketPath());
    ClamdWatcher watcher(&client);

    QSignalSpy changed(&watcher, &ClamdWatcher::statusChanged);
    watcher.checkNow();
    QVERIFY(changed.wait(2000));
    QCOMPARE(watcher.state(), ClamdWatcher::State::Error);

    FakeClamd clamd(socketPath(), kVersionReply);
    watcher.checkNow();
    QVERIFY(changed.wait(2000));
    QCOMPARE(watcher.state(), ClamdWatcher::State::Connected);
    QVERIFY(watcher.errorMessage().isEmpty());
}

void TestClamdWatcher::statusChangedOnlyOnChange()
{
    FakeClamd clamd(socketPath(), kVersionReply);
    ClamdClient client;
    client.setSocketPath(socketPath());
    ClamdWatcher watcher(&client);

    QSignalSpy changed(&watcher, &ClamdWatcher::statusChanged);
    QSignalSpy finished(&watcher, &ClamdWatcher::checkFinished);

    watcher.checkNow();
    QVERIFY(finished.wait(2000));
    watcher.checkNow();
    QVERIFY(finished.wait(2000));

    QCOMPARE(finished.count(), 2);
    QCOMPARE(changed.count(), 1); // deux fois le même état : un seul changement
}

QTEST_GUILESS_MAIN(TestClamdWatcher)
#include "tst_clamdwatcher.moc"
