#include "ClamdClient.h"

#include <QFile>
#include <QFileInfo>
#include <QLocale>
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

} // namespace

ClamdVersion ClamdVersion::fromReply(const QByteArray &reply)
{
    ClamdVersion version;
    const QList<QByteArray> parts = reply.trimmed().split('/');
    const QByteArray prefix = QByteArrayLiteral("ClamAV ");
    if (!parts.first().startsWith(prefix))
        return version;

    version.engine = QString::fromUtf8(parts.first().mid(prefix.size())).trimmed();
    if (parts.size() > 1)
        version.signatures = QString::fromUtf8(parts.at(1)).trimmed();
    if (parts.size() > 2) {
        // Date au format de ctime(), en anglais : "Tue Sep  3 08:26:12 2026"
        // (le jour est complété par une espace, que simplified() supprime).
        const QString date = QString::fromUtf8(parts.at(2)).simplified();
        version.signaturesDate = QLocale::c().toDateTime(date, QStringLiteral("ddd MMM d HH:mm:ss yyyy"));
    }
    return version;
}

QString ClamdClient::errorMessage(Error error, const QString &socketPath, const QString &detail)
{
    switch (error) {
    case Error::NoError:
        return {};
    case Error::SocketNotFound:
        return tr("Socket clamd introuvable : %1\n"
                  "Vérifiez que le service clamd est installé et démarré,\n"
                  "et que le chemin du socket est correct.")
            .arg(socketPath);
    case Error::ConnectionRefused:
        return tr("clamd ne répond pas sur %1 : le service semble arrêté.")
            .arg(socketPath);
    case Error::PermissionDenied: {
        const QString group = socketGroup(socketPath);
        QString user = qEnvironmentVariable("USER");
        if (user.isEmpty())
            user = QStringLiteral("$USER");
        return tr("Permission refusée sur le socket clamd : %1\n"
                  "Ajoutez votre utilisateur au groupe « %2 » :\n"
                  "    sudo usermod -aG %2 %3\n"
                  "puis fermez et rouvrez votre session.")
            .arg(socketPath, group, user);
    }
    case Error::Timeout:
        return tr("clamd n'a pas répondu à temps (%1).").arg(socketPath);
    case Error::ProtocolError:
        return tr("Réponse inattendue de clamd : %1").arg(detail);
    case Error::SocketError:
        return tr("Erreur de communication avec clamd (%1) : %2").arg(socketPath, detail);
    }
    return {};
}

ClamdClient::Error ClamdClient::errorFromSocket(QLocalSocket::LocalSocketError error)
{
    switch (error) {
    case QLocalSocket::ServerNotFoundError:
        return Error::SocketNotFound;
    case QLocalSocket::ConnectionRefusedError:
        return Error::ConnectionRefused;
    case QLocalSocket::SocketAccessError:
        return Error::PermissionDenied;
    case QLocalSocket::SocketTimeoutError:
        return Error::Timeout;
    default:
        return Error::SocketError;
    }
}

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

void ClamdClient::version()
{
    const QString path = m_socketPath;
    sendCommand(QByteArrayLiteral("VERSION"), [this, path](const QByteArray &reply) {
        const ClamdVersion parsed = ClamdVersion::fromReply(reply);
        if (parsed.engine.isEmpty())
            emitError(Error::ProtocolError, path, QString::fromUtf8(reply));
        else
            emit versionReceived(parsed);
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
                if (error == QLocalSocket::PeerClosedError)
                    onClosed();
                else
                    finish(errorFromSocket(error), socket->errorString());
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
    emit errorOccurred(error, errorMessage(error, socketPath, detail));
}
