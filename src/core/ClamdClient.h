#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <functional>

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

signals:
    void pong();
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
