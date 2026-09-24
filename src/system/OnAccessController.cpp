#include "OnAccessController.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

namespace
{
const QString kSystemdService = QStringLiteral("org.freedesktop.systemd1");
const QString kSystemdPath = QStringLiteral("/org/freedesktop/systemd1");
const QString kManagerInterface = QStringLiteral("org.freedesktop.systemd1.Manager");
const QString kUnitInterface = QStringLiteral("org.freedesktop.systemd1.Unit");
const QString kPropertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");

// Valeur d'une clé de /etc/os-release (ID, ID_LIKE...), sans ses guillemets.
QString osReleaseValue(const QString &content, const QString &key)
{
    const QRegularExpression line(QStringLiteral("^%1=\"?([^\"\\n]*)\"?$").arg(key),
                                  QRegularExpression::MultilineOption);
    return line.match(content).captured(1).trimmed();
}
}

OnAccessController::OnAccessController(const QDBusConnection &bus, const QString &logPath, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_log(logPath)
{
    connect(&m_log, &OnAccessLog::historyLoaded, this, &OnAccessController::historyLoaded);
    connect(&m_log, &OnAccessLog::threatDetected, this, &OnAccessController::threatDetected);
    connect(&m_log, &OnAccessLog::errorLogged, this, [this](const QString &error) {
        m_lastError = error;
        updateState();
    });
    // Nouveau lancement de clamonacc : l'erreur du lancement précédent
    // n'expliquerait plus un nouvel échec.
    connect(&m_log, &OnAccessLog::runStarted, this, [this] {
        m_lastError.clear();
        updateState();
    });

    if (m_bus.isConnected()) {
        // systemd signale lui-même les changements d'état du service (démarré,
        // arrêté, en échec)... mais seulement aux clients qui se sont abonnés.
        m_bus.connect(kSystemdService, unitObjectPath(QString::fromLatin1(kServiceName)), kPropertiesInterface,
                      QStringLiteral("PropertiesChanged"), this, SLOT(onUnitPropertiesChanged(QDBusMessage)));
        m_bus.connect(kSystemdService, kSystemdPath, kManagerInterface, QStringLiteral("Reloading"), this,
                      SLOT(onManagerReloading(bool)));
        m_bus.connect(kSystemdService, kSystemdPath, kManagerInterface, QStringLiteral("UnitFilesChanged"), this,
                      SLOT(onUnitFilesChanged()));
        m_bus.asyncCall(QDBusMessage::createMethodCall(kSystemdService, kSystemdPath, kManagerInterface,
                                                       QStringLiteral("Subscribe")));
    }
}

OnAccessController::State OnAccessController::state() const
{
    return m_state;
}

QString OnAccessController::message() const
{
    return m_message;
}

QString OnAccessController::clamonaccPath() const
{
    return m_clamonacc;
}

QStringList OnAccessController::watchedPaths() const
{
    return m_watchedPaths;
}

void OnAccessController::setSearchDirectories(const QStringList &directories)
{
    m_searchDirectories = directories;
}

void OnAccessController::refresh()
{
    m_clamonacc = findClamonacc(m_searchDirectories);
    m_watchedPaths = readWatchedPaths();
    if (!m_bus.isConnected()) {
        m_unit.clear();
        updateState();
        return;
    }

    readUnit(QString::fromLatin1(kServiceName), [this](const QVariantMap &unit) {
        m_unit = unit;
        readUnit(QString::fromLatin1(kDistributionServiceName), [this](const QVariantMap &other) {
            m_distributionServiceActive = other.value(QStringLiteral("ActiveState")).toString() == QLatin1String("active");
            updateState();
        });
    });
}

void OnAccessController::startMonitoring()
{
    m_log.start();
}

void OnAccessController::onUnitPropertiesChanged(const QDBusMessage &)
{
    refresh();
}

void OnAccessController::onManagerReloading(bool active)
{
    if (!active) // fin du rechargement
        refresh();
}

void OnAccessController::onUnitFilesChanged()
{
    refresh();
}

void OnAccessController::readUnit(const QString &unitName, const std::function<void(const QVariantMap &)> &onResult)
{
    // Lecture seule : aucun privilège nécessaire. Pour une unité inconnue,
    // systemd répond quand même, avec LoadState = « not-found ».
    QDBusMessage call = QDBusMessage::createMethodCall(kSystemdService, unitObjectPath(unitName), kPropertiesInterface,
                                                       QStringLiteral("GetAll"));
    call << kUnitInterface;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [onResult](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        const QDBusPendingReply<QVariantMap> reply = *watcher;
        onResult(reply.isError() ? QVariantMap() : reply.value());
    });
}

void OnAccessController::updateState()
{
    const QString service = QString::fromLatin1(kServiceName);
    const QString loadState = m_unit.value(QStringLiteral("LoadState")).toString();
    const QString activeState = m_unit.value(QStringLiteral("ActiveState")).toString();
    const QString subState = m_unit.value(QStringLiteral("SubState")).toString();

    State state = State::Unknown;
    QString message;
    if (m_clamonacc.isEmpty()) {
        state = State::NotInstalled;
        const QString command = installCommand();
        message = command.isEmpty()
            ? tr("clamonacc n'est pas installé : installez le paquet de ClamAV qui le fournit.")
            : tr("clamonacc n'est pas installé. Pour l'installer :\n    %1").arg(command);
    } else if (m_unit.isEmpty()) {
        state = State::Unknown;
        message = tr("État de la protection inconnu : systemd est injoignable.");
    } else if (loadState != QLatin1String("loaded")) {
        state = State::ServiceMissing;
        message = tr("Le service %1 n'est pas installé.\n"
                     "Il est installé par les paquets .deb et .rpm de Linux Defender, "
                     "pas par l'archive .tar.gz.")
                      .arg(service);
    } else if (subState.contains(QLatin1String("auto-restart"))) {
        // clamonacc s'est arrêté sans que personne ne l'ait demandé, même avec
        // le code 0 (il quitte ainsi sur une erreur fatale) : avec Restart=always,
        // systemd le relance (« auto-restart », « dead-before-auto-restart »...).
        // Un arrêt demandé (systemctl stop) mène, lui, à « inactive ».
        state = State::Failed;
        message = (m_lastError.isEmpty()
                       ? tr("clamonacc s'est arrêté de lui-même.\nDétails : journalctl -u %1").arg(service)
                       : explainError(m_lastError))
            + tr("\nsystemd le relance automatiquement.");
    } else if (activeState == QLatin1String("active") || activeState == QLatin1String("reloading")) {
        state = State::Active;
        message = m_watchedPaths.isEmpty()
            ? tr("clamonacc analyse les fichiers ouverts et modifiés.")
            : tr("clamonacc analyse les fichiers ouverts et modifiés dans : %1")
                  .arg(m_watchedPaths.join(QStringLiteral(", ")));
    } else if (activeState == QLatin1String("failed")) {
        state = State::Failed;
        message = m_lastError.isEmpty() ? tr("Le service %1 a échoué.\nDétails : journalctl -u %1").arg(service)
                                        : explainError(m_lastError);
    } else if (activeState == QLatin1String("activating")) {
        state = State::Unknown;
        message = tr("Démarrage de la protection en temps réel…");
    } else {
        state = State::Inactive;
        message = tr("La protection en temps réel est désactivée.");
    }

    // ClamAV crée son journal en 0640 (root:root) : s'il n'a pas été créé
    // lisible à l'avance, l'application ne voit aucune détection.
    const QFileInfo log(m_log.path());
    if (!m_clamonacc.isEmpty() && log.exists() && !log.isReadable()) {
        message += tr("\nAttention : le journal %1 n'est pas lisible par votre utilisateur : les "
                      "détections ne peuvent pas être affichées. Il doit être lisible (droits 0644).")
                       .arg(m_log.path());
    }

    if (m_distributionServiceActive) {
        message += tr("\nAttention : le service %1 de la distribution est actif. Il lance un second "
                      "clamonacc, avec sa propre configuration. Pour l'arrêter :\n"
                      "    sudo systemctl disable --now %1")
                       .arg(QString::fromLatin1(kDistributionServiceName));
    }

    if (state == m_state && message == m_message)
        return;
    m_state = state;
    m_message = message;
    emit stateChanged();
}

QStringList OnAccessController::defaultSearchDirectories()
{
    return {QStringLiteral("/usr/sbin"), QStringLiteral("/usr/bin"), QStringLiteral("/usr/local/sbin"),
            QStringLiteral("/usr/local/bin")};
}

QString OnAccessController::findClamonacc(const QStringList &directories)
{
    // Selon la distribution : /usr/sbin (Debian, Ubuntu) ou /usr/bin (Fedora).
    return QStandardPaths::findExecutable(QStringLiteral("clamonacc"), directories);
}

QString OnAccessController::installCommand(const QString &osReleasePath)
{
    QFile file(osReleasePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QString content = QString::fromUtf8(file.readAll());
    // ID : la distribution ; ID_LIKE : celles dont elle dérive (ubuntu -> debian).
    const QStringList ids = (osReleaseValue(content, QStringLiteral("ID")) + QLatin1Char(' ')
                             + osReleaseValue(content, QStringLiteral("ID_LIKE")))
                                .split(QLatin1Char(' '), Qt::SkipEmptyParts);

    const auto is = [&ids](const char *id) { return ids.contains(QLatin1String(id)); };
    if (is("fedora") || is("rhel") || is("centos"))
        return QStringLiteral("sudo dnf install clamav clamd");
    if (is("debian") || is("ubuntu"))
        return QStringLiteral("sudo apt install clamav-daemon");
    if (is("arch"))
        return QStringLiteral("sudo pacman -S clamav");
    if (is("suse") || is("opensuse"))
        return QStringLiteral("sudo zypper install clamav");
    return {};
}

QStringList OnAccessController::readWatchedPaths(const QString &configPath)
{
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    static const QRegularExpression directive(QStringLiteral("^\\s*OnAccessIncludePath\\s+(.+)$"));
    QStringList paths;
    while (!file.atEnd()) {
        const QRegularExpressionMatch match = directive.match(QString::fromUtf8(file.readLine()).trimmed());
        if (match.hasMatch())
            paths << match.captured(1).trimmed();
    }
    return paths;
}

QString OnAccessController::explainError(const QString &logError)
{
    // Messages relevés sur clamonacc 1.5.4.
    if (logError.contains(QLatin1String("fanotify_init failed")) || logError.contains(QLatin1String("elevated permissions")))
        return tr("clamonacc n'a pas les privilèges nécessaires : fanotify exige les droits root "
                  "(capacité CAP_SYS_ADMIN). Le service doit être lancé par systemd, en root.");
    if (logError.contains(QLatin1String("Could not connect to clamd"))
        || logError.contains(QLatin1String("connection could not be established")))
        return tr("clamonacc ne peut pas joindre clamd : vérifiez que clamd est démarré et que le "
                  "socket indiqué par LocalSocket dans %1 est le bon.")
            .arg(QString::fromLatin1(kConfigPath));
    // « ClamInotif: could not watch path '/home', No space left on device » :
    // limite atteinte au démarrage, clamonacc s'arrête aussitôt.
    if (logError.startsWith(QLatin1String("ClamInotif: could not watch path"))
        && logError.contains(QLatin1String("No space left on device")))
        return tr("Trop de dossiers à surveiller : augmentez la limite du noyau "
                  "fs.inotify.max_user_watches (voir le README).");
    return tr("Erreur de clamonacc : %1\nDétails : journalctl -u %2").arg(logError, QString::fromLatin1(kServiceName));
}

QString OnAccessController::unitObjectPath(const QString &unitName)
{
    // Échappement de systemd : lettres (et chiffres, sauf en tête) gardées,
    // tout autre octet devient « _xx » (hexadécimal).
    QString path = QStringLiteral("/org/freedesktop/systemd1/unit/");
    const QByteArray bytes = unitName.toUtf8();
    for (qsizetype i = 0; i < bytes.size(); ++i) {
        const char c = bytes.at(i);
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        const bool digit = c >= '0' && c <= '9';
        if (letter || (digit && i > 0))
            path += QLatin1Char(c);
        else
            path += QStringLiteral("_%1").arg(uchar(c), 2, 16, QLatin1Char('0'));
    }
    return path;
}
