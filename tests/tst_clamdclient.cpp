#include "FakeClamd.h"
#include "core/ClamdClient.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

class TestClamdClient : public QObject
{
    Q_OBJECT

private slots:
    void pingSucceeds();
    void pingSocketNotFound();
    void pingConnectionRefused();
    void pingPermissionDenied();
    void pingUnexpectedReply();
    void pingTimeout();
    void versionSucceeds();
    void versionParsing_data();
    void versionParsing();

private:
    QString socketPath() const { return m_dir.filePath(QString::fromLatin1(QTest::currentTestFunction())); }

    // Lance un PING et renvoie l'erreur émise (NoError si aucune).
    static ClamdClient::Error pingError(ClamdClient &client, QString *message = nullptr)
    {
        QSignalSpy spy(&client, &ClamdClient::errorOccurred);
        client.ping();
        if (!spy.wait(2000))
            return ClamdClient::Error::NoError;
        if (message)
            *message = spy.first().at(1).toString();
        return spy.first().at(0).value<ClamdClient::Error>();
    }

    QTemporaryDir m_dir;
};

void TestClamdClient::pingSucceeds()
{
    FakeClamd clamd(socketPath(), QByteArrayLiteral("PONG\0"));
    ClamdClient client;
    client.setSocketPath(socketPath());

    QSignalSpy pong(&client, &ClamdClient::pong);
    QSignalSpy error(&client, &ClamdClient::errorOccurred);
    client.ping();

    QVERIFY(pong.wait(2000));
    QCOMPARE(error.count(), 0);
    QCOMPARE(clamd.received(), QByteArrayLiteral("zPING\0"));
}

void TestClamdClient::pingSocketNotFound()
{
    ClamdClient client;
    client.setSocketPath(socketPath());
    QCOMPARE(pingError(client), ClamdClient::Error::SocketNotFound);
}

void TestClamdClient::pingConnectionRefused()
{
    // Un fichier ordinaire à la place du socket : comme un socket resté après un arrêt de clamd.
    QFile stale(socketPath());
    QVERIFY(stale.open(QIODevice::WriteOnly));
    stale.close();

    ClamdClient client;
    client.setSocketPath(socketPath());
    QCOMPARE(pingError(client), ClamdClient::Error::ConnectionRefused);
}

void TestClamdClient::pingPermissionDenied()
{
    if (geteuid() == 0)
        QSKIP("root ignore les permissions des fichiers");

    FakeClamd clamd(socketPath(), QByteArrayLiteral("PONG\0"));
    QVERIFY(QFile::setPermissions(socketPath(), QFileDevice::Permissions()));

    ClamdClient client;
    client.setSocketPath(socketPath());
    QString message;
    QCOMPARE(pingError(client, &message), ClamdClient::Error::PermissionDenied);
    QVERIFY2(message.contains(QLatin1String("usermod -aG")), qPrintable(message));
}

void TestClamdClient::pingUnexpectedReply()
{
    FakeClamd clamd(socketPath(), QByteArrayLiteral("UNKNOWN COMMAND\0"));
    ClamdClient client;
    client.setSocketPath(socketPath());
    QCOMPARE(pingError(client), ClamdClient::Error::ProtocolError);
}

void TestClamdClient::pingTimeout()
{
    FakeClamd clamd(socketPath(), QByteArray());
    ClamdClient client;
    client.setSocketPath(socketPath());
    client.setTimeout(200);
    QCOMPARE(pingError(client), ClamdClient::Error::Timeout);
}

void TestClamdClient::versionSucceeds()
{
    FakeClamd clamd(socketPath(), QByteArrayLiteral("ClamAV 1.4.2/27400/Tue Sep 23 08:26:12 2025\0"));
    ClamdClient client;
    client.setSocketPath(socketPath());

    QSignalSpy spy(&client, &ClamdClient::versionReceived);
    client.version();

    QVERIFY(spy.wait(2000));
    QCOMPARE(clamd.received(), QByteArrayLiteral("zVERSION\0"));
    const auto version = spy.first().at(0).value<ClamdVersion>();
    QCOMPARE(version.engine, QStringLiteral("1.4.2"));
    QCOMPARE(version.signatures, QStringLiteral("27400"));
}

void TestClamdClient::versionParsing_data()
{
    QTest::addColumn<QByteArray>("reply");
    QTest::addColumn<QString>("engine");
    QTest::addColumn<QString>("signatures");
    QTest::addColumn<QDateTime>("date");

    QTest::newRow("complète") << QByteArray("ClamAV 1.4.2/27400/Tue Sep 23 08:26:12 2025")
                              << QStringLiteral("1.4.2") << QStringLiteral("27400")
                              << QDateTime(QDate(2025, 9, 23), QTime(8, 26, 12));
    QTest::newRow("jour sur un chiffre") << QByteArray("ClamAV 1.4.2/27400/Wed Sep  3 06:00:00 2025")
                                         << QStringLiteral("1.4.2") << QStringLiteral("27400")
                                         << QDateTime(QDate(2025, 9, 3), QTime(6, 0));
    QTest::newRow("moteur seul") << QByteArray("ClamAV 1.4.2")
                                 << QStringLiteral("1.4.2") << QString() << QDateTime();
    QTest::newRow("réponse inconnue") << QByteArray("UNKNOWN COMMAND")
                                      << QString() << QString() << QDateTime();
}

void TestClamdClient::versionParsing()
{
    QFETCH(QByteArray, reply);
    QFETCH(QString, engine);
    QFETCH(QString, signatures);
    QFETCH(QDateTime, date);

    const ClamdVersion version = ClamdVersion::fromReply(reply);
    QCOMPARE(version.engine, engine);
    QCOMPARE(version.signatures, signatures);
    QCOMPARE(version.signaturesDate, date);
}

QTEST_GUILESS_MAIN(TestClamdClient)
#include "tst_clamdclient.moc"
