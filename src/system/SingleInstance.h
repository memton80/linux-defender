#pragma once

#include <QByteArray>
#include <QLocalServer>
#include <QLockFile>
#include <QObject>

/**
 * Empêche de lancer l'application deux fois.
 *
 * La première instance prend un verrou (QLockFile) et ouvre un socket local.
 * Les suivantes échouent à prendre le verrou : elles envoient alors un message
 * à la première (par exemple « show » pour réafficher la fenêtre), puis quittent.
 *
 * Verrou et socket sont dans XDG_RUNTIME_DIR (/run/user/<uid>), propre à
 * chaque utilisateur. Un verrou laissé par une instance plantée est détecté
 * et remplacé automatiquement par QLockFile.
 */
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    // `directory` vide : XDG_RUNTIME_DIR. Les tests utilisent un dossier temporaire.
    explicit SingleInstance(const QString &name, const QString &directory = {}, QObject *parent = nullptr);

    // Renvoie true si cette instance est la première. Sinon, transmet
    // `message` (s'il n'est pas vide) à l'instance déjà lancée et renvoie false.
    bool tryBecomePrimary(const QByteArray &message);

signals:
    // Reçu par l'instance principale, envoyé par une instance suivante.
    void messageReceived(const QByteArray &message);

private:
    void sendToPrimary(const QByteArray &message);

    QString m_socketPath;
    QLockFile m_lock;
    QLocalServer m_server;
};
