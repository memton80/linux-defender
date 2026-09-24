#include "system/SingleInstance.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestSingleInstance : public QObject
{
    Q_OBJECT

private slots:
    void firstInstanceIsPrimary();
    void secondInstanceForwardsMessage();
    void backgroundInstanceSendsNothing();
    void nextInstanceIsPrimaryAfterQuit();
};

void TestSingleInstance::firstInstanceIsPrimary()
{
    QTemporaryDir dir;
    SingleInstance instance(QStringLiteral("app"), dir.path());
    QVERIFY(instance.tryBecomePrimary("show"));
}

void TestSingleInstance::secondInstanceForwardsMessage()
{
    QTemporaryDir dir;
    SingleInstance first(QStringLiteral("app"), dir.path());
    QVERIFY(first.tryBecomePrimary("show"));
    QSignalSpy received(&first, &SingleInstance::messageReceived);

    SingleInstance second(QStringLiteral("app"), dir.path());
    QVERIFY(!second.tryBecomePrimary("show"));

    QVERIFY(received.wait(2000));
    QCOMPARE(received.first().first().toByteArray(), QByteArray("show"));
}

void TestSingleInstance::backgroundInstanceSendsNothing()
{
    QTemporaryDir dir;
    SingleInstance first(QStringLiteral("app"), dir.path());
    QVERIFY(first.tryBecomePrimary("show"));
    QSignalSpy received(&first, &SingleInstance::messageReceived);

    // Lancement avec --background (démarrage de session) : rien à réafficher.
    SingleInstance second(QStringLiteral("app"), dir.path());
    QVERIFY(!second.tryBecomePrimary(QByteArray()));
    QVERIFY(!received.wait(300));
}

void TestSingleInstance::nextInstanceIsPrimaryAfterQuit()
{
    QTemporaryDir dir;
    {
        SingleInstance first(QStringLiteral("app"), dir.path());
        QVERIFY(first.tryBecomePrimary("show"));
    }
    SingleInstance next(QStringLiteral("app"), dir.path());
    QVERIFY(next.tryBecomePrimary("show"));
}

QTEST_GUILESS_MAIN(TestSingleInstance)
#include "tst_singleinstance.moc"
