#include "ClamdClient.h"

#include <QFile>
#include <QFileInfo>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QTimer>

#include <memory>

namespace {

// Fichiers de configuration de clamd selon les distributions.
constexpr const char *kConfigFiles[] = {
    "/etc/clamd.d/scan.conf", // Fedora / RHEL (service clamd@scan)
    "/etc/clamav/clamd.conf", // Debian / Ubuntu / Arch
    "/etc/clamd.conf",        // openSUSE
};

// Emplacements usuels du socket, si aucune configuration n'a été trouvée.
constexpr const char *kSocketCandidates[] = {
    "/run/clamd.scan/clamd.sock", // Fedora / RHEL
    "/run/clamav/clamd.ctl",      // Debian / Ubuntu / Arch
};

constexpr const char *kFallbackSocket = "/run/clamav/clamd.ctl";

// Renvoie la valeur de la directive « LocalSocket » (lignes non commentées).
QString socketFromConfig(const QString &configFile)
{
    QFile file(configFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    static const QRegularExpression directive(QStringLiteral("^\\s*LocalSocket\\s+(\\S+)"));
    while (!file.atEnd()) {
        const QRegularExpressionMatch match = directive.match(QString::fromUtf8(file.readLine()));
        if (match.hasMatch())
            return match.captured(1);
    }
    return {};
}

// Groupe que l'utilisateur doit rejoindre pour accéder au socket : celui du
// dossier s'il n'est pas traversable, sinon celui du socket lui-même.
QString socketGroup(const QString &socketPath)
{
    const QFileInfo socket(socketPath);
    const QFileInfo dir(socket.absolutePath());

    QString group;
    if (dir.exists() && !dir.isExecutable())
        group = dir.group();
    else if (socket.exists())
        group = socket.group();

    if (group.isEmpty() || group == QLatin1String("root"))
        group = QStringLiteral("clamav");
    return group;
}

QString describeError(ClamdClient::Error error, const QString &socketPath, const QString &detail)
{
    switch (error) {
    case ClamdClient::Error::NoError:
        return {};
    case ClamdClient::Error::SocketNotFound:
        return ClamdClient::tr("Socket clamd introuvable : %1\n"
                               "Vérifiez que le service clamd est installé et démarré, "
                               "et que le chemin du socket est correct.")
            .arg(socketPath);
    case ClamdClient::Error::ConnectionRefused:
        return ClamdClient::tr("clamd ne répond pas sur %1 : le service semble arrêté.")
            .arg(socketPath);
    case ClamdClient::Error::PermissionDenied: {
        const QString group = socketGroup(socketPath);
        QString user = qEnvironmentVariable("USER");
        if (user.isEmpty())
            user = QStringLiteral("$USER");
        return ClamdClient::tr("Permission refusée sur le socket clamd : %1\n"
                               "Ajoutez votre utilisateur au groupe « %2 » :\n"
                               "    sudo usermod -aG %2 %3\n"
                               "puis fermez et rouvrez votre session.")
            .arg(socketPath, group, user);
    }
    case ClamdClient::Error::Timeout:
        return ClamdClient::tr("clamd n'a pas répondu à temps (%1).").arg(socketPath);
    case ClamdClient::Error::ProtocolError:
        return ClamdClient::tr("Réponse inattendue de clamd : %1").arg(detail);
    case ClamdClient::Error::SocketError:
        return ClamdClient::tr("Erreur de communication avec clamd (%1) : %2").arg(socketPath, detail);
    }
    return {};
}

} // namespace

ClamdClient::ClamdClient(QObject *parent)
    : QObject(parent)
    , m_socketPath(detectSocketPath())
{
}

QString ClamdClient::socketPath() const
{
    return m_socketPath;
}

void ClamdClient::setSocketPath(const QString &path)
{
    m_socketPath = path;
}

int ClamdClient::timeout() const
{
    return m_timeout;
}

void ClamdClient::setTimeout(int msecs)
{
    m_timeout = msecs;
}

QString ClamdClient::detectSocketPath()
{
    for (const char *config : kConfigFiles) {
        const QString path = socketFromConfig(QString::fromLatin1(config));
        if (!path.isEmpty())
            return path;
    }

    // On teste le dossier plutôt que le socket : si l'utilisateur n'a pas le
    // droit de traverser le dossier, le socket est invisible pour lui. On veut
    // alors signaler « permission refusée », pas « socket introuvable ».
    for (const char *candidate : kSocketCandidates) {
        const QString path = QString::fromLatin1(candidate);
        if (QFileInfo::exists(QFileInfo(path).absolutePath()))
            return path;
    }
    return QString::fromLatin1(kFallbackSocket);
}

void ClamdClient::ping()
{
    const QString path = m_socketPath;
    sendCommand(QByteArrayLiteral("PING"), [this, path](const QByteArray &reply) {
        if (reply == "PONG")
            emit pong();
        else
            emitError(Error::ProtocolError, path, QString::fromUtf8(reply));
    });
}

void ClamdClient::sendCommand(const QByteArray &command, ReplyHandler onReply)
{
    // Une connexion par commande : clamd la ferme après avoir répondu.
    const QString path = m_socketPath;
    auto *socket = new QLocalSocket(this);
    auto *timer = new QTimer(socket);
    timer->setSingleShot(true);
    auto reply = std::make_shared<QByteArray>();

    // Termine la requête, une seule fois : libère le socket, puis transmet le
    // résultat. socket->disconnect() coupe tous les signaux du socket, donc
    // rien de ce qui suit (fermeture, erreur...) ne peut la terminer à nouveau.
    auto finish = [this, socket, timer, reply, onReply, path](Error error, const QString &detail) {
        timer->stop();
        socket->disconnect();
        socket->abort();
        socket->deleteLater();
        if (error == Error::NoError)
            onReply(*reply);
        else
            emitError(error, path, detail);
    };

    // Lit ce qui est arrivé ; si la réponse est complète (octet nul reçu), on termine.
    auto readReply = [socket, reply, finish] {
        reply->append(socket->readAll());
        const qsizetype end = reply->indexOf('\0');
        if (end < 0)
            return false;
        reply->truncate(end);
        finish(Error::NoError, {});
        return true;
    };

    // clamd a fermé la connexion : soit la réponse est complète, soit c'est une erreur.
    auto onClosed = [readReply, finish] {
        if (!readReply())
            finish(Error::ProtocolError, tr("connexion fermée sans réponse complète"));
    };

    connect(socket, &QLocalSocket::connected, socket, [socket, command] {
        QByteArray packet;
        packet.reserve(command.size() + 2);
        packet.append('z').append(command).append('\0');
        socket->write(packet);
    });
    connect(socket, &QLocalSocket::readyRead, socket, readReply);
    connect(socket, &QLocalSocket::disconnected, socket, onClosed);
    connect(socket, &QLocalSocket::errorOccurred, socket,
            [socket, finish, onClosed](QLocalSocket::LocalSocketError error) {
                switch (error) {
                case QLocalSocket::ServerNotFoundError:
                    finish(Error::SocketNotFound, {});
                    break;
                case QLocalSocket::ConnectionRefusedError:
                    finish(Error::ConnectionRefused, {});
                    break;
                case QLocalSocket::SocketAccessError:
                    finish(Error::PermissionDenied, {});
                    break;
                case QLocalSocket::SocketTimeoutError:
                    finish(Error::Timeout, {});
                    break;
                case QLocalSocket::PeerClosedError:
                    onClosed();
                    break;
                default:
                    finish(Error::SocketError, socket->errorString());
                    break;
                }
            });
    connect(timer, &QTimer::timeout, socket, [finish] {
        finish(Error::Timeout, {});
    });

    timer->start(m_timeout);
    // Connexion lancée depuis la boucle d'événements : le résultat arrive donc
    // toujours après le retour de cette fonction, même en cas d'échec immédiat
    // (socket absent). L'appelant peut connecter ses signaux avant ou après.
    QMetaObject::invokeMethod(socket, [socket, path] {
        socket->connectToServer(path);
    }, Qt::QueuedConnection);
}

void ClamdClient::emitError(Error error, const QString &socketPath, const QString &detail)
{
    emit errorOccurred(error, describeError(error, socketPath, detail));
}
