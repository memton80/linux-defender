#include "core/ClamdClient.h"

#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

// Faux clamd : écoute sur un socket Unix et renvoie une réponse fixe à chaque
// commande, puis ferme la connexion (comme le vrai clamd). Permet de tester
// ClamdClient sans avoir clamd installé.
class FakeClamd : public QObject
{
public:
    // `reply` nul (QByteArray()) : ne répond jamais, pour tester le délai d'attente.
    FakeClamd(const QString &path, const QByteArray &reply)
        : m_reply(reply)
    {
        connect(&m_server, &QLocalServer::newConnection, this, [this] {
            QLocalSocket *client = m_server.nextPendingConnection();
            connect(client, &QLocalSocket::readyRead, this, [this, client] {
                received += client->readAll();
                if (received.endsWith('\0') && !m_reply.isNull()) {
                    client->write(m_reply);
                    client->disconnectFromServer();
                }
            });
        });
        QVERIFY(m_server.listen(path));
    }

    QByteArray received;

private:
    QLocalServer m_server;
    QByteArray m_reply;
};

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
    FakeClamd clamd(socketPath(), QByteArray("PONG\0", 5));
    ClamdClient client;
    client.setSocketPath(socketPath());

    QSignalSpy pong(&client, &ClamdClient::pong);
    QSignalSpy error(&client, &ClamdClient::errorOccurred);
    client.ping();

    QVERIFY(pong.wait(2000));
    QCOMPARE(error.count(), 0);
    QCOMPARE(clamd.received, QByteArray("zPING\0", 6));
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

    FakeClamd clamd(socketPath(), QByteArray("PONG\0", 5));
    QVERIFY(QFile::setPermissions(socketPath(), QFileDevice::Permissions()));

    ClamdClient client;
    client.setSocketPath(socketPath());
    QString message;
    QCOMPARE(pingError(client, &message), ClamdClient::Error::PermissionDenied);
    QVERIFY2(message.contains(QLatin1String("usermod -aG")), qPrintable(message));
}

void TestClamdClient::pingUnexpectedReply()
{
    FakeClamd clamd(socketPath(), QByteArray("UNKNOWN COMMAND\0", 16));
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

QTEST_GUILESS_MAIN(TestClamdClient)
#include "tst_clamdclient.moc"
