#include "PrivilegedHelper.h"

#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace
{
// Codes de sortie de pkexec (voir pkexec(1)).
constexpr int kPkexecDismissed = 126; // fenêtre d'authentification fermée
constexpr int kPkexecNotAuthorized = 127;
// Codes du programme d'aide (voir son en-tête).
constexpr int kHelperUsage = 2;
}

PrivilegedHelper::PrivilegedHelper(QObject *parent)
    : QObject(parent)
    , m_launcher(QStandardPaths::findExecutable(QStringLiteral("pkexec")))
    , m_helper(QString::fromLatin1(kHelperPath))
{
}

PrivilegedHelper::~PrivilegedHelper()
{
    // Une action en cours va jusqu'au bout (on n'interrompt pas systemctl) :
    // le processus est seulement détaché de l'application qui quitte.
    if (m_process) {
        m_process->disconnect(this);
        m_process->setParent(nullptr);
        connect(m_process, &QProcess::finished, m_process, &QObject::deleteLater);
    }
}

bool PrivilegedHelper::isAvailable() const
{
    return !m_launcher.isEmpty() && QFileInfo(m_helper).isExecutable();
}

bool PrivilegedHelper::isRunning() const
{
    return m_process != nullptr;
}

void PrivilegedHelper::setCommand(const QString &launcher, const QString &helper)
{
    m_launcher = launcher;
    m_helper = helper;
}

QStringList PrivilegedHelper::helperArguments(Action action, const QString &argument)
{
    switch (action) {
    case Action::OnAccessEnable:
        return {QStringLiteral("onaccess-enable")};
    case Action::OnAccessDisable:
        return {QStringLiteral("onaccess-disable")};
    case Action::ClamdStart:
        return {QStringLiteral("clamd-start")};
    case Action::ClamdConfigure:
        return {QStringLiteral("clamd-configure")};
    case Action::ClamdAlertExceedsMax:
        return {QStringLiteral("clamd-alert-exceeds-max")};
    case Action::FreshclamEnable:
        return {QStringLiteral("freshclam-enable")};
    case Action::SelinuxAllowScan:
        return {QStringLiteral("selinux-allow-scan")};
    case Action::InotifyRaise:
        return {QStringLiteral("inotify-raise")};
    case Action::SocketGroupAdd:
        return {QStringLiteral("socket-group-add"), argument};
    }
    return {};
}

PrivilegedHelper::Result PrivilegedHelper::resultFromExit(int exitCode, const QString &errorOutput, QString *message)
{
    const QString output = errorOutput.trimmed();
    switch (exitCode) {
    case 0:
        message->clear();
        return Result::Success;
    case kPkexecDismissed:
        *message = tr("Authentification annulée : rien n'a été modifié.");
        return Result::Cancelled;
    case kPkexecNotAuthorized:
        *message = tr("Action refusée : votre compte n'est pas autorisé à administrer le système, ou "
                      "l'authentification a échoué.");
        return Result::Failed;
    case kHelperUsage:
        *message = tr("Appel invalide du programme d'aide (%1).").arg(output);
        return Result::Failed;
    default:
        break;
    }
    *message = output.isEmpty() ? tr("L'action a échoué (code %1).").arg(exitCode) : output;
    return Result::Failed;
}

void PrivilegedHelper::run(Action action, const QString &argument)
{
    if (m_process)
        return;
    m_action = action;
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        QString message;
        Result result = Result::Failed;
        if (status == QProcess::NormalExit)
            result = resultFromExit(exitCode, QString::fromLocal8Bit(m_process->readAllStandardError()), &message);
        else
            message = tr("Le programme d'aide s'est arrêté brutalement.");
        m_process->deleteLater();
        m_process = nullptr;
        emit finished(m_action, result, message);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return; // les autres erreurs sont suivies de finished()
        const QString message = tr("Impossible de lancer %1 : %2").arg(m_launcher, m_process->errorString());
        m_process->deleteLater();
        m_process = nullptr;
        emit finished(m_action, Result::Failed, message);
    });

    emit started(action);
    m_process->start(m_launcher, QStringList{m_helper} + helperArguments(action, argument));
}
