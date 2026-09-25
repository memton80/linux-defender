#include "SingleInstance.h"

#include <QDebug>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QThread>

namespace
{
const QString kActionKey = QStringLiteral("action");
const QString kPathsKey = QStringLiteral("paths");
const QString kTokenKey = QStringLiteral("activationToken");

QString actionName(InstanceRequest::Action action)
{
    switch (action) {
    case InstanceRequest::Action::Scan:
        return QStringLiteral("scan");
    case InstanceRequest::Action::QuickScan:
        return QStringLiteral("quick-scan");
    case InstanceRequest::Action::Show:
        break;
    }
    return QStringLiteral("show");
}

QString baseDirectory(const QString &directory)
{
    return directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) : directory;
}
}

QByteArray InstanceRequest::encode() const
{
    // Sans rien d'autre à transmettre : « show », que comprennent aussi les versions 1.0.x.
    if (action == Action::Show && activationToken.isEmpty())
        return QByteArrayLiteral("show");
    QJsonObject object{{kActionKey, actionName(action)}};
    if (!paths.isEmpty())
        object.insert(kPathsKey, QJsonArray::fromStringList(paths));
    if (!activationToken.isEmpty())
        object.insert(kTokenKey, activationToken);
    // Compact : une seule ligne, les sauts de ligne des chemins sont échappés.
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

std::optional<InstanceRequest> InstanceRequest::decode(const QByteArray &message)
{
    if (message == "show")
        return InstanceRequest{};
    const QJsonObject object = QJsonDocument::fromJson(message).object();
    InstanceRequest request;
    const QString action = object.value(kActionKey).toString();
    if (action == QLatin1String("scan"))
        request.action = Action::Scan;
    else if (action == QLatin1String("quick-scan"))
        request.action = Action::QuickScan;
    else if (action != QLatin1String("show"))
        return std::nullopt;
    for (const QJsonValue &path : object.value(kPathsKey).toArray())
        request.paths << path.toString();
    request.activationToken = object.value(kTokenKey).toString();
    if (request.action == Action::Scan && request.paths.isEmpty())
        return std::nullopt;
    return request;
}

SingleInstance::SingleInstance(const QString &name, const QString &directory, QObject *parent)
    : QObject(parent)
    , m_socketPath(QDir(baseDirectory(directory)).filePath(name + QStringLiteral(".sock")))
    , m_lock(QDir(baseDirectory(directory)).filePath(name + QStringLiteral(".lock")))
{
    // 0 : un verrou n'est jamais considéré comme périmé à cause de son âge,
    // seulement si le processus qui le détient n'existe plus.
    m_lock.setStaleLockTime(0);

    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *client = m_server.nextPendingConnection()) {
            connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
            connect(client, &QLocalSocket::readyRead, this, [this, client] {
                while (client->canReadLine())
                    emit messageReceived(client->readLine().trimmed());
            });
        }
    });
}

bool SingleInstance::tryBecomePrimary(const QByteArray &message)
{
    if (!m_lock.tryLock(0)) {
        if (!message.isEmpty())
            sendToPrimary(message);
        return false;
    }

    // Nous détenons le verrou : un socket existant ne peut être qu'un reste
    // d'une instance plantée, on peut le supprimer.
    QLocalServer::removeServer(m_socketPath);
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server.listen(m_socketPath))
        qWarning() << "Instance unique : écoute impossible sur" << m_socketPath << ":" << m_server.errorString();
    return true;
}

void SingleInstance::sendToPrimary(const QByteArray &message)
{
    // L'autre instance vient peut-être de démarrer et n'écoute pas encore :
    // on réessaie pendant 2 secondes. Appel bloquant, mais très court, et
    // fait au démarrage avant l'affichage de toute fenêtre.
    for (int attempt = 0; attempt < 20; ++attempt) {
        QLocalSocket socket;
        socket.connectToServer(m_socketPath);
        if (socket.waitForConnected(100)) {
            socket.write(message + '\n');
            socket.waitForBytesWritten(1000);
            socket.disconnectFromServer();
            return;
        }
        QThread::msleep(100);
    }
    qWarning() << "Instance unique : l'instance déjà lancée ne répond pas.";
}
