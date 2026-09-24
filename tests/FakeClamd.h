#pragma once

#include <QLocalServer>
#include <QLocalSocket>
#include <QTest>

// Faux clamd : écoute sur un socket Unix et renvoie une réponse fixe à chaque
// commande, puis ferme la connexion (comme le vrai clamd). Permet de tester
// le code sans avoir clamd installé.
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
