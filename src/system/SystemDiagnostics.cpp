#include "SystemDiagnostics.h"

#include "core/Settings.h"

#include <QCoreApplication>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLocale>

#include <grp.h>
#include <pwd.h>
#include <unistd.h>

#include <algorithm>

namespace
{
const QString kSystemdService = QStringLiteral("org.freedesktop.systemd1");
const QString kUnitInterface = QStringLiteral("org.freedesktop.systemd1.Unit");
const QString kPropertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
const QString kFreshclamService = QStringLiteral("clamav-freshclam.service");

QString tr(const char *text)
{
    return QCoreApplication::translate("SystemDiagnostics", text);
}

bool isAny(const QStringList &distribution, std::initializer_list<const char *> ids)
{
    return std::any_of(ids.begin(), ids.end(), [&](const char *id) { return distribution.contains(QLatin1String(id)); });
}

QString readFirstLine(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromLatin1(file.readLine()).trimmed();
}

QString megabytes(qint64 bytes)
{
    return QLocale().toString(double(bytes) / (1024 * 1024), 'g', 4);
}

// Nom du service à redémarrer dans les commandes affichées.
QString serviceOrClamd(const DiagnosticInput &input)
{
    return input.clamdService.isEmpty() ? QStringLiteral("clamd") : input.clamdService;
}

DiagnosticItem clamdItem(const DiagnosticInput &input)
{
    DiagnosticItem item;
    item.id = QStringLiteral("clamd");
    switch (input.clamdState) {
    case ClamdWatcher::State::Connected:
        item.title = tr("clamd répond");
        item.text = tr("ClamAV %1, socket %2.").arg(input.version.engine, input.socketPath);
        return item;
    case ClamdWatcher::State::Unknown:
        item.level = DiagnosticItem::Level::Info;
        item.title = tr("Vérification de clamd en cours…");
        return item;
    case ClamdWatcher::State::Error:
        break;
    }

    item.level = DiagnosticItem::Level::Error;
    const QString config = input.config.path;
    const QString service = serviceOrClamd(input);

    if (input.clamdError == ClamdClient::Error::PermissionDenied) {
        if (input.userInSocketGroup && !input.sessionHasSocketGroup) {
            item.level = DiagnosticItem::Level::Warning;
            item.title = tr("Session à rouvrir");
            item.text = tr("Votre utilisateur est bien dans le groupe « %1 », propriétaire du socket de clamd, mais "
                           "cette session a été ouverte avant son ajout : fermez la session et rouvrez-la (ou "
                           "redémarrez).")
                            .arg(input.socketGroup);
            return item;
        }
        item.title = tr("Accès refusé au socket de clamd");
        item.text = tr("Le socket %1 est réservé au groupe « %2 ». Ajoutez-y votre utilisateur, puis fermez la session "
                       "et rouvrez-la.")
                        .arg(input.socketPath, input.socketGroup);
        item.command = QStringLiteral("sudo usermod -aG %1 %2").arg(input.socketGroup, input.userName);
        item.fix = PrivilegedHelper::Action::SocketGroupAdd;
        item.fixArgument = input.socketGroup;
        item.fixLabel = tr("Ajouter au groupe « %1 »").arg(input.socketGroup);
        return item;
    }

    if (input.clamdError != ClamdClient::Error::SocketNotFound
        && input.clamdError != ClamdClient::Error::ConnectionRefused) {
        item.title = tr("clamd ne répond pas correctement");
        item.text = input.clamdErrorMessage;
        item.command = input.clamdService.isEmpty() ? QString()
                                                    : QStringLiteral("journalctl -u %1 -n 30").arg(input.clamdService);
        return item;
    }

    // Socket absent ou personne n'écoute.
    if (input.config.exampleLine) {
        item.title = tr("Configuration de clamd inachevée");
        item.text = tr("La ligne « Example » de %1 est active : clamd refuse de démarrer tant qu'elle n'est pas "
                       "commentée (réglage d'origine sous Fedora). La correction la commente, active le socket et "
                       "démarre clamd. Une copie du fichier d'origine est gardée (%1.linux-defender-orig).")
                        .arg(config);
        item.command = QStringLiteral("sudo sed -i 's/^Example/#Example/' %1\nsudoedit %1   # décommentez LocalSocket\n"
                                      "sudo systemctl enable --now %2")
                           .arg(config, service);
        item.fix = PrivilegedHelper::Action::ClamdConfigure;
        item.fixLabel = tr("Terminer la configuration");
        return item;
    }
    if (!config.isEmpty() && input.config.localSocket.isEmpty()) {
        item.title = tr("Socket de clamd désactivé");
        item.text = tr("Aucune directive LocalSocket active dans %1 : clamd n'ouvre pas de socket, et "
                       "l'application ne peut pas lui transmettre de fichiers (réglage d'origine sous Fedora). "
                       "Une copie du fichier d'origine est gardée (%1.linux-defender-orig).")
                        .arg(config);
        item.command = QStringLiteral("sudoedit %1   # décommentez LocalSocket\nsudo systemctl restart %2").arg(config, service);
        item.fix = PrivilegedHelper::Action::ClamdConfigure;
        item.fixLabel = tr("Activer le socket");
        return item;
    }
    if (input.clamdService.isEmpty()) {
        item.title = tr("clamd n'est pas installé");
        item.text = tr("Aucun service clamd (clamd@scan, clamav-daemon ou clamd) n'est installé : l'antivirus ne "
                       "peut rien analyser.");
        item.command = SystemDiagnostics::clamdInstallCommand(input.distribution);
        return item;
    }
    const QString state = input.clamdServiceState;
    if (state == QLatin1String("activating") || state == QLatin1String("reloading")) {
        item.level = DiagnosticItem::Level::Info;
        item.title = tr("clamd démarre");
        item.text = tr("Chargement des signatures en cours : cela peut prendre une minute.");
        return item;
    }
    if (state == QLatin1String("failed")) {
        item.title = tr("clamd a échoué au démarrage");
        item.text = tr("Le service %1 s'est arrêté sur une erreur. Cause fréquente : aucune base de signatures "
                       "(téléchargez-les avec freshclam). Le détail est dans le journal du service.")
                        .arg(input.clamdService);
        item.command = QStringLiteral("journalctl -u %1 -n 30\nsudo freshclam\nsudo systemctl restart %1")
                           .arg(input.clamdService);
        item.fix = PrivilegedHelper::Action::ClamdStart;
        item.fixLabel = tr("Redémarrer clamd");
        return item;
    }
    if (state == QLatin1String("active")) {
        item.title = tr("clamd tourne, mais pas sur ce socket");
        item.text = tr("Le service %1 est démarré, mais rien ne répond sur %2. Vérifiez la directive LocalSocket "
                       "de %3, ou le socket choisi dans les paramètres.")
                        .arg(input.clamdService, input.socketPath,
                             config.isEmpty() ? tr("la configuration de clamd") : config);
        return item;
    }
    item.title = tr("clamd est arrêté");
    item.text = tr("Le service %1 n'est pas démarré : aucune analyse n'est possible. La correction le démarre, et "
                   "l'active au démarrage de la machine.")
                    .arg(input.clamdService);
    item.command = QStringLiteral("sudo systemctl enable --now %1").arg(input.clamdService);
    item.fix = PrivilegedHelper::Action::ClamdStart;
    item.fixLabel = tr("Démarrer clamd");
    return item;
}

std::optional<DiagnosticItem> signaturesItem(const DiagnosticInput &input)
{
    if (input.clamdState != ClamdWatcher::State::Connected)
        return std::nullopt;

    DiagnosticItem item;
    item.id = QStringLiteral("signatures");
    const auto proposeFreshclam = [&] {
        if (!input.freshclamInstalled) {
            item.command = SystemDiagnostics::freshclamInstallCommand(input.distribution);
        } else if (!input.freshclamActive) {
            item.text += QLatin1Char(' ')
                + tr("Le service clamav-freshclam, qui les télécharge, est arrêté.");
            item.command = QStringLiteral("sudo systemctl enable --now clamav-freshclam");
            item.fix = PrivilegedHelper::Action::FreshclamEnable;
            item.fixLabel = tr("Activer les mises à jour");
        } else {
            item.text += QLatin1Char(' ')
                + tr("Le service clamav-freshclam tourne mais n'y arrive pas (réseau, miroir...) : voir son journal.");
            item.command = QStringLiteral("journalctl -u clamav-freshclam -n 30");
        }
    };

    if (input.version.signatures.isEmpty()) {
        item.level = DiagnosticItem::Level::Error;
        item.title = tr("Aucune base de signatures");
        item.text = tr("clamd n'a chargé aucune signature : il ne peut rien détecter.");
        proposeFreshclam();
        return item;
    }
    const QDateTime date = input.version.signaturesDate;
    const qint64 days = date.isValid() ? qMax<qint64>(0, date.secsTo(QDateTime::currentDateTime()) / 86400) : -1;
    if (input.signaturesMaxAge > 0 && days >= input.signaturesMaxAge) {
        item.level = DiagnosticItem::Level::Warning;
        item.title = tr("Signatures obsolètes (%1 jours)").arg(days);
        item.text = tr("Les signatures n° %1 ne détectent pas les menaces apparues depuis.").arg(input.version.signatures);
        proposeFreshclam();
        return item;
    }
    item.title = tr("Signatures à jour");
    item.text = date.isValid() ? tr("Signatures n° %1 du %2.")
                                     .arg(input.version.signatures, QLocale().toString(date, QLocale::ShortFormat))
                               : tr("Signatures n° %1.").arg(input.version.signatures);
    return item;
}

std::optional<DiagnosticItem> selinuxItem(const DiagnosticInput &input)
{
    if (!input.selinuxEnforcing || !input.antivirusCanScanSystem)
        return std::nullopt;
    DiagnosticItem item;
    item.id = QStringLiteral("selinux");
    if (*input.antivirusCanScanSystem) {
        item.title = tr("SELinux autorise clamd à lire vos fichiers");
        return item;
    }
    item.level = DiagnosticItem::Level::Warning;
    item.title = tr("SELinux empêche clamd de lire vos fichiers");
    item.text = tr("Le booléen antivirus_can_scan_system est désactivé : clamd ne peut pas lire les fichiers de "
                   "votre dossier personnel ni ceux des clés USB, même transmis par l'application, et les analyses "
                   "finissent en erreur (« Permission denied »).");
    item.command = QStringLiteral("sudo setsebool -P antivirus_can_scan_system 1");
    item.fix = PrivilegedHelper::Action::SelinuxAllowScan;
    item.fixLabel = tr("Autoriser l'analyse");
    return item;
}

std::optional<DiagnosticItem> limitsItem(const DiagnosticInput &input)
{
    if (input.config.path.isEmpty())
        return std::nullopt;
    DiagnosticItem item;
    item.id = QStringLiteral("limits");
    const QString fileLimit = input.config.maxFileSize > 0 ? tr("%1 Mo").arg(megabytes(input.config.unscannedAbove()))
                                                          : tr("aucune (2 Go au plus)");
    if (input.config.alertExceedsMax) {
        item.title = tr("Analyses incomplètes signalées");
        item.text = tr("Un fichier trop gros pour clamd (au-delà de %1) ou une archive au contenu trop volumineux "
                       "(au-delà de %2 Mo) est signalé « non analysé » (AlertExceedsMax).")
                        .arg(fileLimit, megabytes(input.config.maxScanSize));
        return item;
    }
    item.level = DiagnosticItem::Level::Info;
    item.title = tr("Archives analysées en partie sans avertissement");
    item.text = tr("Les fichiers plus gros que la limite de clamd (%1) sont signalés « non analysé » par "
                   "l'application. Mais quand le contenu d'une archive dépasse %2 Mo (MaxScanSize), clamd n'en "
                   "analyse que le début et répond « sain ». Avec AlertExceedsMax, ces archives sont signalées "
                   "« non analysé » (clamd redémarre : une minute sans analyse).")
                    .arg(fileLimit, megabytes(input.config.maxScanSize));
    item.command = QStringLiteral("echo 'AlertExceedsMax yes' | sudo tee -a %1\nsudo systemctl restart %2")
                       .arg(input.config.path, serviceOrClamd(input));
    item.fix = PrivilegedHelper::Action::ClamdAlertExceedsMax;
    item.fixLabel = tr("Signaler les archives incomplètes");
    return item;
}

bool onAccessInstalled(OnAccessController::State state)
{
    return state == OnAccessController::State::Active || state == OnAccessController::State::Inactive
        || state == OnAccessController::State::Failed;
}

std::optional<DiagnosticItem> onAccessItem(const DiagnosticInput &input)
{
    DiagnosticItem item;
    item.id = QStringLiteral("onaccess");
    const QString service = QString::fromLatin1(OnAccessController::kServiceName);
    switch (input.onAccessState) {
    case OnAccessController::State::Active:
        item.title = tr("Protection en temps réel active");
        return item;
    case OnAccessController::State::Inactive:
        item.level = DiagnosticItem::Level::Info;
        item.title = tr("Protection en temps réel désactivée");
        item.text = tr("Les menaces ne sont détectées que lors des analyses.");
        item.command = QStringLiteral("sudo systemctl enable --now %1").arg(service);
        item.fix = PrivilegedHelper::Action::OnAccessEnable;
        item.fixLabel = tr("Activer la protection");
        return item;
    case OnAccessController::State::Failed:
        item.level = DiagnosticItem::Level::Error;
        item.title = tr("Protection en temps réel en erreur");
        item.text = input.onAccessMessage;
        item.command = QStringLiteral("journalctl -u %1 -n 30").arg(service);
        return item;
    case OnAccessController::State::NotInstalled:
        item.level = DiagnosticItem::Level::Info;
        item.title = tr("clamonacc n'est pas installé");
        item.text = tr("La protection en temps réel en a besoin.");
        item.command = OnAccessController::installCommand();
        return item;
    case OnAccessController::State::ServiceMissing:
        item.level = DiagnosticItem::Level::Info;
        item.title = tr("Service de protection en temps réel absent");
        item.text = tr("Il est installé par les paquets .deb et .rpm de Linux Defender, pas par l'archive .tar.gz.");
        return item;
    case OnAccessController::State::Unknown:
        break;
    }
    return std::nullopt;
}

std::optional<DiagnosticItem> inotifyItem(const DiagnosticInput &input)
{
    if (!onAccessInstalled(input.onAccessState) || input.inotifyMaxWatches < 0)
        return std::nullopt;
    DiagnosticItem item;
    item.id = QStringLiteral("inotify");
    const QString limit = QLocale().toString(input.inotifyMaxWatches);
    const bool low = input.inotifyMaxWatches < SystemDiagnostics::kLowInotifyWatches;
    if (!input.inotifyLimitReached && !low) {
        item.title = tr("Limite de dossiers surveillés suffisante");
        item.text = tr("Le noyau permet de surveiller %1 dossiers (fs.inotify.max_user_watches).").arg(limit);
        return item;
    }
    item.level = input.inotifyLimitReached ? DiagnosticItem::Level::Error : DiagnosticItem::Level::Info;
    item.title = input.inotifyLimitReached ? tr("Trop de dossiers à surveiller") : tr("Limite de dossiers surveillés basse");
    item.text = (input.inotifyLimitReached
                     ? tr("clamonacc s'est arrêté : les dossiers surveillés en contiennent plus que la limite du noyau "
                          "(%1).")
                     : tr("Le noyau ne permet de surveiller que %1 dossiers : un dossier personnel bien rempli "
                          "(projets de développement, caches) peut dépasser cette limite."))
                    .arg(limit)
        + QLatin1Char(' ') + tr("La correction la porte à 524 288, durablement.");
    item.command = QStringLiteral("echo fs.inotify.max_user_watches=524288 | sudo tee /etc/sysctl.d/90-linux-defender.conf\n"
                                  "sudo sysctl --system");
    item.fix = PrivilegedHelper::Action::InotifyRaise;
    item.fixLabel = tr("Relever la limite");
    return item;
}
}

SystemDiagnostics::SystemDiagnostics(ClamdWatcher *watcher, OnAccessController *onAccess, const QDBusConnection &bus,
                                     const QString &root, QObject *parent)
    : QObject(parent)
    , m_watcher(watcher)
    , m_onAccess(onAccess)
    , m_bus(bus)
    , m_root(root)
{
    connect(m_watcher, &ClamdWatcher::statusChanged, this, &SystemDiagnostics::refresh);
    connect(m_onAccess, &OnAccessController::stateChanged, this, &SystemDiagnostics::refresh);
}

QList<DiagnosticItem> SystemDiagnostics::items() const
{
    return m_items;
}

DiagnosticItem::Level SystemDiagnostics::worstLevel() const
{
    DiagnosticItem::Level level = DiagnosticItem::Level::Ok;
    for (const DiagnosticItem &item : m_items)
        level = std::max(level, item.level);
    return level;
}

std::optional<DiagnosticItem> SystemDiagnostics::mostSevere() const
{
    std::optional<DiagnosticItem> worst;
    for (const DiagnosticItem &item : m_items) {
        if (item.level >= DiagnosticItem::Level::Warning && (!worst || item.level > worst->level))
            worst = item;
    }
    return worst;
}

QList<DiagnosticItem> SystemDiagnostics::evaluate(const DiagnosticInput &input)
{
    QList<DiagnosticItem> items{clamdItem(input)};
    for (const std::optional<DiagnosticItem> &item :
         {signaturesItem(input), selinuxItem(input), limitsItem(input), onAccessItem(input), inotifyItem(input)}) {
        if (item)
            items << *item;
    }
    return items;
}

QStringList SystemDiagnostics::clamdServices()
{
    // Même ordre que ClamdConfig::defaultFiles() et le programme d'aide.
    return {QStringLiteral("clamd@scan.service"), QStringLiteral("clamav-daemon.service"),
            QStringLiteral("clamd.service")};
}

QString SystemDiagnostics::clamdInstallCommand(const QStringList &distribution)
{
    if (isAny(distribution, {"fedora", "rhel", "centos"}))
        return QStringLiteral("sudo dnf install clamd clamav-update");
    if (isAny(distribution, {"debian", "ubuntu"}))
        return QStringLiteral("sudo apt install clamav-daemon clamav-freshclam");
    if (isAny(distribution, {"arch"}))
        return QStringLiteral("sudo pacman -S clamav");
    if (isAny(distribution, {"suse", "opensuse"}))
        return QStringLiteral("sudo zypper install clamav");
    return {};
}

QString SystemDiagnostics::freshclamInstallCommand(const QStringList &distribution)
{
    if (isAny(distribution, {"fedora", "rhel", "centos"}))
        return QStringLiteral("sudo dnf install clamav-update");
    if (isAny(distribution, {"debian", "ubuntu"}))
        return QStringLiteral("sudo apt install clamav-freshclam");
    return QStringLiteral("sudo freshclam");
}

void SystemDiagnostics::readSelinux(const QString &root, bool *enforcing, std::optional<bool> *antivirusCanScanSystem)
{
    const QDir dir(root);
    *enforcing = readFirstLine(dir.filePath(QStringLiteral("sys/fs/selinux/enforce"))) == QLatin1String("1");
    // « 1 1 » : valeur actuelle, puis valeur en attente d'application.
    const QString boolean = readFirstLine(dir.filePath(QStringLiteral("sys/fs/selinux/booleans/antivirus_can_scan_system")));
    if (boolean.isEmpty())
        antivirusCanScanSystem->reset();
    else
        *antivirusCanScanSystem = boolean.section(QLatin1Char(' '), 0, 0) == QLatin1String("1");
}

qint64 SystemDiagnostics::readInotifyMaxWatches(const QString &root)
{
    bool ok = false;
    const qint64 value = readFirstLine(QDir(root).filePath(QStringLiteral("proc/sys/fs/inotify/max_user_watches"))).toLongLong(&ok);
    return ok ? value : -1;
}

void SystemDiagnostics::refresh()
{
    if (m_pending > 0) {
        m_refreshAgain = true; // relevé en cours : un autre suivra
        return;
    }

    DiagnosticInput &input = m_input;
    input.clamdState = m_watcher->state();
    input.clamdError = m_watcher->error();
    input.clamdErrorMessage = m_watcher->errorMessage();
    input.version = m_watcher->version();
    input.socketPath = m_watcher->socketPath();
    input.config = ClamdConfig::forSocket(input.socketPath);
    input.signaturesMaxAge = Settings::signaturesMaxAge();
    input.distribution = OnAccessController::distributionIds(QDir(m_root).filePath(QStringLiteral("etc/os-release")));

    // Groupe du socket : dans la base des groupes, et dans la session en cours
    // (un ajout ne vaut qu'après une nouvelle connexion).
    input.socketGroup = ClamdClient::socketGroup(input.socketPath);
    input.userInSocketGroup = false;
    input.sessionHasSocketGroup = false;
    const passwd *user = ::getpwuid(::getuid());
    input.userName = user ? QString::fromLocal8Bit(user->pw_name) : qEnvironmentVariable("USER");
    if (const group *socketGroup = ::getgrnam(input.socketGroup.toLocal8Bit().constData())) {
        const gid_t gid = socketGroup->gr_gid;
        input.userInSocketGroup = user && user->pw_gid == gid;
        for (char **member = socketGroup->gr_mem; member && *member && user; ++member)
            input.userInSocketGroup = input.userInSocketGroup || qstrcmp(*member, user->pw_name) == 0;
        QList<gid_t> groups(qMax(0, ::getgroups(0, nullptr)));
        if (::getgroups(int(groups.size()), groups.data()) < 0)
            groups.clear();
        input.sessionHasSocketGroup = groups.contains(gid) || ::getegid() == gid;
    }

    readSelinux(m_root, &input.selinuxEnforcing, &input.antivirusCanScanSystem);
    input.onAccessState = m_onAccess->state();
    input.onAccessMessage = m_onAccess->message();
    input.inotifyLimitReached = m_onAccess->inotifyLimitReached();
    input.inotifyMaxWatches = readInotifyMaxWatches(m_root);

    // Services systemd, en lecture seule : clamd et freshclam.
    m_services.clear();
    if (!m_bus.isConnected()) {
        finishRefresh();
        return;
    }
    const QStringList services = clamdServices() << kFreshclamService;
    m_pending = int(services.size());
    for (const QString &service : services)
        readService(service);
}

void SystemDiagnostics::readService(const QString &name)
{
    // Pour une unité inconnue, systemd répond quand même, avec LoadState = « not-found ».
    QDBusMessage call = QDBusMessage::createMethodCall(kSystemdService, OnAccessController::unitObjectPath(name),
                                                       kPropertiesInterface, QStringLiteral("GetAll"));
    call << kUnitInterface;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, name](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        const QDBusPendingReply<QVariantMap> reply = *watcher;
        if (!reply.isError())
            m_services.insert(name, reply.value());
        if (--m_pending == 0)
            finishRefresh();
    });
}

void SystemDiagnostics::finishRefresh()
{
    const auto loaded = [this](const QString &name) {
        return m_services.value(name).value(QStringLiteral("LoadState")).toString() == QLatin1String("loaded");
    };
    const auto activeState = [this](const QString &name) {
        return m_services.value(name).value(QStringLiteral("ActiveState")).toString();
    };

    m_input.clamdService.clear();
    m_input.clamdServiceState.clear();
    for (const QString &service : clamdServices()) {
        if (loaded(service)) {
            m_input.clamdService = service;
            m_input.clamdServiceState = activeState(service);
            break;
        }
    }
    m_input.freshclamInstalled = loaded(kFreshclamService);
    m_input.freshclamActive = activeState(kFreshclamService) == QLatin1String("active");

    m_items = evaluate(m_input);
    emit changed();

    if (m_refreshAgain) {
        m_refreshAgain = false;
        refresh();
    }
}
