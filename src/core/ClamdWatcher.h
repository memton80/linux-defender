#pragma once

#include "ClamdClient.h"

#include <QDateTime>
#include <QObject>
#include <QTimer>

/**
 * Surveille la disponibilité de clamd : envoie VERSION à intervalle régulier
 * et garde le dernier état connu. L'icône de notification et la fenêtre
 * principale affichent cet état.
 *
 * VERSION plutôt que PING : la commande prouve aussi que clamd répond, et
 * donne en plus la version du moteur et la date des signatures.
 */
class ClamdWatcher : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Unknown,   // aucune vérification terminée pour l'instant
        Connected, // clamd répond : voir version()
        Error,     // clamd injoignable : voir errorMessage()
    };
    Q_ENUM(State)

    // `client` n'est pas possédé : il doit vivre plus longtemps que le watcher.
    explicit ClamdWatcher(ClamdClient *client, QObject *parent = nullptr);

    State state() const;
    ClamdVersion version() const;  // renseigné si state() == Connected
    QString errorMessage() const;  // renseigné si state() == Error
    QDateTime lastCheck() const;   // invalide tant qu'aucune vérification n'est terminée
    QString socketPath() const;

    // Intervalle entre deux vérifications (30 s par défaut).
    void setInterval(int msecs);

    // Vérifie tout de suite, puis à intervalle régulier.
    void start();

    // Vérifie tout de suite. Sans effet si une vérification est déjà en cours.
    void checkNow();

signals:
    // Émis seulement quand l'état, la version ou le message d'erreur change.
    void statusChanged();
    // Émis à la fin de chaque vérification, même si rien n'a changé.
    void checkFinished();

private:
    void finishCheck(State state, const ClamdVersion &version, const QString &errorMessage);

    ClamdClient *m_client;
    QTimer m_timer;
    bool m_checking = false;
    State m_state = State::Unknown;
    ClamdVersion m_version;
    QString m_errorMessage;
    QDateTime m_lastCheck;
};
