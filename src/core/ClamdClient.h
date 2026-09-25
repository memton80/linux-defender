#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QLocalSocket>
#include <QObject>
#include <QString>

#include <functional>

// Réponse à la commande VERSION, par exemple :
//   "ClamAV 1.4.2/27400/Tue Sep 23 08:26:12 2026"
// c'est-à-dire : version du moteur / version des signatures / date des signatures.
struct ClamdVersion
{
    QString engine;             // "1.4.2"
    QString signatures;         // "27400" (vide si clamd n'a chargé aucune base)
    QDateTime signaturesDate;   // invalide si absente ou illisible

    // Renvoie une version vide (engine vide) si la réponse n'est pas reconnue.
    static ClamdVersion fromReply(const QByteArray &reply);

    bool operator==(const ClamdVersion &other) const
    {
        return engine == other.engine && signatures == other.signatures && signaturesDate == other.signaturesDate;
    }
    bool operator!=(const ClamdVersion &other) const { return !(*this == other); }
};

/**
 * Client du démon clamd, via son socket Unix local.
 *
 * Utilise le protocole natif de clamd (voir `man clamd`, section COMMANDS) :
 * chaque commande est préfixée par 'z' et terminée par un octet nul
 * ("zPING\0"), et clamd répond avec une chaîne elle aussi terminée par un
 * octet nul ("PONG\0").
 *
 * Tout est asynchrone : aucune méthode ne bloque, les résultats arrivent par
 * signal. L'UI peut donc l'utiliser sans jamais se figer.
 *
 * Cette classe ne dépend que de QtCore et QtNetwork, jamais de l'UI.
 */
class ClamdClient : public QObject
{
    Q_OBJECT

public:
    enum class Error {
        NoError,
        SocketNotFound,    // le socket n'existe pas : clamd absent, arrêté, ou mauvais chemin
        ConnectionRefused, // le socket existe mais personne n'écoute : clamd arrêté
        PermissionDenied,  // droits insuffisants sur le socket ou sur son dossier
        Timeout,           // clamd n'a pas répondu dans le délai imparti
        ProtocolError,     // réponse inattendue de clamd
        SocketError,       // autre erreur système
    };
    Q_ENUM(Error)

    explicit ClamdClient(QObject *parent = nullptr);

    // Par défaut : le chemin trouvé par detectSocketPath().
    QString socketPath() const;
    void setSocketPath(const QString &path);

    // Délai maximal (en ms) pour une commande : connexion + envoi + réponse.
    int timeout() const;
    void setTimeout(int msecs);

    // Cherche le socket de clamd : d'abord la directive LocalSocket des
    // fichiers de configuration de clamd, puis les emplacements usuels.
    static QString detectSocketPath();

    // Envoie PING. Résultat : pong() si clamd répond, sinon errorOccurred().
    void ping();

    // Envoie VERSION. Résultat : versionReceived(), sinon errorOccurred().
    void version();

    // Message lisible par l'utilisateur pour une erreur (utilisé aussi par ScanJob).
    static QString errorMessage(Error error, const QString &socketPath, const QString &detail = {});
    // Groupe que l'utilisateur doit rejoindre pour accéder au socket : celui du
    // dossier s'il n'est pas traversable, sinon celui du socket lui-même.
    static QString socketGroup(const QString &socketPath);
    // Traduit une erreur de QLocalSocket en erreur ClamdClient.
    static Error errorFromSocket(QLocalSocket::LocalSocketError error);

signals:
    void pong();
    void versionReceived(const ClamdVersion &version);
    // `message` est prêt à être affiché à l'utilisateur.
    void errorOccurred(ClamdClient::Error error, const QString &message);

private:
    using ReplyHandler = std::function<void(const QByteArray &reply)>;

    // Ouvre une connexion, envoie une commande, et passe la réponse (sans son
    // octet nul final) à `onReply`. Les erreurs sont émises via errorOccurred().
    void sendCommand(const QByteArray &command, ReplyHandler onReply);
    void emitError(Error error, const QString &socketPath, const QString &detail = {});

    QString m_socketPath;
    int m_timeout = 5000;
};
