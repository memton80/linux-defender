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
    void requestEncoding();
    void rejectsUnknownRequests();
    void forwardsScanRequest();
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

void TestSingleInstance::requestEncoding()
{
    // Sans rien d'autre : « show », compris aussi par les versions 1.0.x.
    QCOMPARE(InstanceRequest().encode(), QByteArray("show"));
    QCOMPARE(int(InstanceRequest::decode("show")->action), int(InstanceRequest::Action::Show));

    // Chemins difficiles (espaces, accents, saut de ligne) : une seule ligne, rien de perdu.
    InstanceRequest scan;
    scan.action = InstanceRequest::Action::Scan;
    scan.paths = {QStringLiteral("/home/alex/Téléchargements/facture (1).pdf"), QStringLiteral("/tmp/a\nb"),
                  QStringLiteral("/run/media/alex/CLÉ USB")};
    scan.activationToken = QStringLiteral("kwin-42");
    const QByteArray encoded = scan.encode();
    QVERIFY(!encoded.contains('\n'));
    const std::optional<InstanceRequest> decoded = InstanceRequest::decode(encoded);
    QVERIFY(decoded);
    QCOMPARE(int(decoded->action), int(InstanceRequest::Action::Scan));
    QCOMPARE(decoded->paths, scan.paths);
    QCOMPARE(decoded->activationToken, scan.activationToken);

    InstanceRequest quick;
    quick.action = InstanceRequest::Action::QuickScan;
    QCOMPARE(int(InstanceRequest::decode(quick.encode())->action), int(InstanceRequest::Action::QuickScan));

    // Affichage avec jeton d'activation (Wayland) : JSON.
    InstanceRequest show;
    show.activationToken = QStringLiteral("kwin-43");
    QCOMPARE(InstanceRequest::decode(show.encode())->activationToken, QStringLiteral("kwin-43"));
}

void TestSingleInstance::rejectsUnknownRequests()
{
    QVERIFY(!InstanceRequest::decode("hide"));
    QVERIFY(!InstanceRequest::decode(R"({"action":"format"})"));
    QVERIFY(!InstanceRequest::decode(R"({"action":"scan"})")); // analyse sans chemin
    QVERIFY(!InstanceRequest::decode("{pas du json"));
}

void TestSingleInstance::forwardsScanRequest()
{
    QTemporaryDir dir;
    SingleInstance first(QStringLiteral("app"), dir.path());
    QVERIFY(first.tryBecomePrimary(InstanceRequest().encode()));
    QSignalSpy received(&first, &SingleInstance::messageReceived);

    InstanceRequest scan;
    scan.action = InstanceRequest::Action::Scan;
    scan.paths = {QStringLiteral("/home/alex/a b.txt"), QStringLiteral("/home/alex/dossier")};
    SingleInstance second(QStringLiteral("app"), dir.path());
    QVERIFY(!second.tryBecomePrimary(scan.encode()));

    QVERIFY(received.wait(2000));
    const std::optional<InstanceRequest> request = InstanceRequest::decode(received.first().first().toByteArray());
    QVERIFY(request);
    QCOMPARE(request->paths, scan.paths);
}

QTEST_GUILESS_MAIN(TestSingleInstance)
#include "tst_singleinstance.moc"
