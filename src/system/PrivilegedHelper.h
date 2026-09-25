#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;

/**
 * Lance les corrections qui demandent les droits root : programme d'aide
 * linux-defender-helper (installé par les paquets .deb et .rpm), exécuté par
 * pkexec sous l'action polkit io.github.memton80.linux-defender.manage.
 *
 * L'agent polkit du bureau (celui de Plasma...) demande le mot de passe
 * administrateur, retenu quelques minutes. L'application elle-même ne tourne
 * jamais en root, et ne transmet au programme d'aide qu'un nom d'action de
 * sa liste fermée (et, pour le groupe du socket, un nom de groupe qu'il
 * vérifie lui-même).
 *
 * Une action à la fois : run() est sans effet tant que la précédente n'est
 * pas terminée.
 */
class PrivilegedHelper : public QObject
{
    Q_OBJECT

public:
    enum class Action {
        OnAccessEnable,
        OnAccessDisable,
        ClamdStart,
        ClamdConfigure,
        ClamdAlertExceedsMax,
        FreshclamEnable,
        SelinuxAllowScan,
        InotifyRaise,
        SocketGroupAdd, // argument : groupe du socket de clamd
    };
    Q_ENUM(Action)

    enum class Result {
        Success,
        Cancelled, // authentification annulée par l'utilisateur
        Failed,    // refusée, ou échec de l'action : voir le message
    };
    Q_ENUM(Result)

    static constexpr const char *kHelperPath = DEFENDER_HELPER_PATH;

    explicit PrivilegedHelper(QObject *parent = nullptr);
    ~PrivilegedHelper() override;

    // Programme d'aide et pkexec installés ? Sinon (archive .tar.gz, pkexec
    // absent), l'application affiche seulement les commandes à lancer.
    bool isAvailable() const;
    bool isRunning() const;

    void run(Action action, const QString &argument = {});

    // Tests : commande lancée à la place de « pkexec <programme d'aide> ».
    void setCommand(const QString &launcher, const QString &helper);

    // Arguments du programme d'aide pour une action.
    static QStringList helperArguments(Action action, const QString &argument = {});
    // Résultat d'après le code de sortie de pkexec (126 : annulé, 127 : non
    // autorisé) ou du programme d'aide, et le message à afficher.
    static Result resultFromExit(int exitCode, const QString &errorOutput, QString *message);

signals:
    void started(PrivilegedHelper::Action action);
    // `message` : explication en cas d'échec (vide sinon).
    void finished(PrivilegedHelper::Action action, PrivilegedHelper::Result result, const QString &message);

private:
    QString m_launcher;
    QString m_helper;
    QProcess *m_process = nullptr;
    Action m_action = Action::OnAccessEnable;
};
