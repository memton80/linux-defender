#include "TrayIcon.h"

#include "StatusDisplay.h"
#include "core/ClamdWatcher.h"
#include "system/OnAccessController.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace
{
constexpr int kNotificationDuration = 10000; // ms
constexpr int kMaxThreatsInNotification = 3;

// Clés des notifications : une nouvelle notification remplace celle de même clé.
const QString kRealtimeKey = QStringLiteral("realtime");        // détections en temps réel
const QString kScanKey = QStringLiteral("scan");                // scan de clé USB : début, fin, échec
const QString kScanThreatsKey = QStringLiteral("scan-threats"); // menaces trouvées par un scan

// Nom d'icône du thème (Breeze sous Plasma) ; sinon l'icône de l'application,
// copiée hors des ressources pour que le serveur de notifications puisse la lire.
QString notificationIcon(const QString &themeName, const QString &resource)
{
    if (QIcon::hasThemeIcon(themeName))
        return themeName;
    static QSet<QString> copied; // copie refaite à chaque lancement : l'icône a pu changer
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/icons");
    const QString file = dir + QLatin1Char('/') + QFileInfo(resource).fileName();
    if (!copied.contains(file) && QDir().mkpath(dir)) {
        QFile::remove(file);
        if (QFile::copy(resource, file))
            copied.insert(file);
    }
    return copied.contains(file) ? file : QString();
}

// Ouvre le gestionnaire de fichiers (Dolphin...) sur le dossier, fichier
// sélectionné ; à défaut, ouvre simplement le dossier.
void showInFileManager(const QString &path, const QString &activationToken)
{
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("/org/freedesktop/FileManager1"),
        QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("ShowItems"));
    call << QStringList{QUrl::fromLocalFile(path).toString()} << activationToken;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), qApp);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, qApp, [path](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        if (watcher->isError())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    });
}
}

TrayIcon::TrayIcon(ClamdWatcher *watcher, ScanManager *scans, OnAccessController *onAccess, QObject *parent)
    : QSystemTrayIcon(parent)
    , m_watcher(watcher)
    , m_scans(scans)
    , m_onAccess(onAccess)
    // Nom affiché et fichier .desktop : Plasma y rattache les notifications.
    , m_notifier(QGuiApplication::applicationDisplayName(), QGuiApplication::desktopFileName())
{
    // Premières lignes : l'état de clamd et de la protection en temps réel,
    // pour information (non cliquables).
    m_statusAction = m_menu.addAction(QString());
    m_statusAction->setEnabled(false);
    m_onAccessAction = m_menu.addAction(QString());
    m_onAccessAction->setEnabled(false);

    QAction *check = m_menu.addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), tr("Vérifier l'état de clamd"));
    connect(check, &QAction::triggered, m_watcher, &ClamdWatcher::checkNow);

    m_menu.addSeparator();

    m_scanFolderAction = m_menu.addAction(QIcon::fromTheme(QStringLiteral("folder-open")), tr("Scanner un dossier…"));
    connect(m_scanFolderAction, &QAction::triggered, this, &TrayIcon::scanFolderRequested);

    m_stopAction = m_menu.addAction(QIcon::fromTheme(QStringLiteral("process-stop")), tr("Arrêter le scan"));
    connect(m_stopAction, &QAction::triggered, m_scans, &ScanManager::cancelAll);

    m_menu.addSeparator();

    QAction *open = m_menu.addAction(QIcon::fromTheme(QStringLiteral("window")), tr("Ouvrir la fenêtre"));
    connect(open, &QAction::triggered, this, &TrayIcon::showWindowRequested);

    QAction *quit = m_menu.addAction(QIcon::fromTheme(QStringLiteral("application-exit")), tr("Quitter"));
    connect(quit, &QAction::triggered, qApp, &QCoreApplication::quit);

    setContextMenu(&m_menu);

    // Clic gauche sur l'icône : afficher ou masquer la fenêtre.
    connect(this, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger)
            emit toggleWindowRequested();
    });
    connect(&m_notifier, &DesktopNotifier::actionInvoked, this, &TrayIcon::onNotificationAction);
    connect(&m_notifier, &DesktopNotifier::failed, this, &TrayIcon::notifyFallback);
    // Alerte fermée : les détections suivantes feront une nouvelle alerte.
    connect(&m_notifier, &DesktopNotifier::closed, this, [this](const QString &key) {
        if (key == kRealtimeKey)
            m_realtimeThreats.clear();
    });
    // Sans service de notification (showMessage) : un clic ouvre la fenêtre.
    connect(this, &QSystemTrayIcon::messageClicked, this, [this] {
        if (m_lastMessageRealtime)
            emit showOnAccessRequested();
        else
            emit showWindowRequested();
    });

    connect(m_watcher, &ClamdWatcher::statusChanged, this, &TrayIcon::updateState);
    connect(m_scans, &ScanManager::scanStarted, this, &TrayIcon::onScanStarted);
    connect(m_scans, &ScanManager::resultsReady, this, &TrayIcon::onResults);
    connect(m_scans, &ScanManager::scanFinished, this, &TrayIcon::onScanFinished);
    connect(m_onAccess, &OnAccessController::stateChanged, this, &TrayIcon::updateState);
    connect(m_onAccess, &OnAccessController::threatDetected, this, &TrayIcon::onRealtimeThreat);
    updateState();
}

void TrayIcon::acknowledgeThreats()
{
    if (!m_threatsPending)
        return;
    m_threatsPending = false;
    // Les détections sont sous les yeux de l'utilisateur : l'alerte, qui
    // resterait affichée jusqu'à sa fermeture, n'a plus lieu d'être.
    m_notifier.close(kRealtimeKey);
    m_realtimeThreats.clear();
    updateState();
}

void TrayIcon::onScanStarted(const QStringList &paths, ScanManager::Origin origin)
{
    m_scanThreats.clear();
    // Scan automatique : on prévient l'utilisateur, qui ne l'a pas demandé
    // (et ne pourra pas éjecter la clé tant que le scan lit ses fichiers).
    if (origin == ScanManager::Origin::Usb) {
        DesktopNotifier::Notification notification;
        notification.title = tr("Scan de la clé USB");
        notification.body = tr("Analyse de %1 en cours…").arg(StatusDisplay::pathsText(paths));
        notification.icon = notificationIcon(QStringLiteral("drive-removable-media-usb"),
                                             QStringLiteral(":/icons/status-scanning.svg"));
        notification.timeoutMsecs = kNotificationDuration;
        notification.actions = {{QStringLiteral("default"), tr("Ouvrir la fenêtre")}};
        notify(kScanKey, notification, StatusDisplay::scanningIcon());
    }
    updateState();
}

void TrayIcon::onResults(const QList<ScanResult> &results)
{
    // Seules les premières menaces sont citées dans la notification : inutile de garder les autres.
    for (const ScanResult &result : results) {
        if (result.status == ScanResult::Status::Infected && m_scanThreats.size() < kMaxThreatsInNotification)
            m_scanThreats.append({result.path, result.detail});
    }
}

void TrayIcon::onScanFinished(const ScanSummary &summary, ScanManager::Origin origin)
{
    const QString paths = StatusDisplay::pathsText(summary.paths);
    const bool usb = origin == ScanManager::Origin::Usb;

    DesktopNotifier::Notification notification;
    notification.timeoutMsecs = kNotificationDuration;
    notification.actions = {{QStringLiteral("default"), tr("Ouvrir la fenêtre")}};
    if (summary.infected > 0) {
        m_threatsPending = true;
        m_threatSummary = tr("Menaces détectées dans %1").arg(paths);
        const ThreatText::Alert alert =
            ThreatText::scanAlert(m_scanThreats, summary.infected, StatusDisplay::summaryText(summary));
        notification.title = alert.title;
        notification.body = alert.body;
        notification.icon = notificationIcon(QStringLiteral("security-low"), QStringLiteral(":/icons/result-threat.svg"));
        notification.actions.append({QStringLiteral("details"), tr("Afficher les détails")});
        // Scan d'une clé USB, que l'utilisateur n'a pas lancé : l'alerte reste
        // affichée. Un scan lancé depuis la fenêtre a ses résultats sous les yeux.
        if (usb) {
            notification.urgency = DesktopNotifier::Urgency::Critical;
            notification.timeoutMsecs = 0;
        }
        m_notifier.close(kScanKey); // « Analyse en cours… »
        notify(kScanThreatsKey, notification, StatusDisplay::threatIcon());
    } else if (!summary.fatalError.isEmpty()) {
        notification.title = tr("Scan impossible");
        notification.body = summary.fatalError;
        notification.icon = notificationIcon(QStringLiteral("dialog-error"), QStringLiteral(":/icons/result-warning.svg"));
        notify(kScanKey, notification, StatusDisplay::resultIcon(ScanResult::Status::Error));
    } else if (usb && !summary.cancelled) {
        notification.title = tr("Clé USB analysée");
        notification.body = tr("%1\n%2").arg(paths, StatusDisplay::summaryText(summary));
        notification.icon = notificationIcon(QStringLiteral("security-high"), QStringLiteral(":/icons/status-ok.svg"));
        notify(kScanKey, notification, StatusDisplay::resultIcon(ScanResult::Status::Clean));
    }
    m_scanThreats.clear();
    updateState();
}

void TrayIcon::onRealtimeThreat(const OnAccessDetection &detection)
{
    m_threatsPending = true;
    m_threatSummary = tr("Menace détectée en temps réel : %1").arg(ThreatText::shortPath(detection.path));
    // Toutes les détections pas encore consultées tiennent dans une seule
    // alerte, mise à jour (une archive décompressée ne fait pas 20 alertes).
    // Un fichier déjà signalé (rouvert, par exemple) n'y change rien.
    const bool known = std::any_of(m_realtimeThreats.cbegin(), m_realtimeThreats.cend(), [&](const ThreatText::Threat &threat) {
        return threat.path == detection.path && threat.name == detection.threat;
    });
    if (!known) {
        m_realtimeThreats.append({detection.path, detection.threat});
        showRealtimeAlert();
    }
    updateState();
}

void TrayIcon::showRealtimeAlert()
{
    const ThreatText::Alert alert = ThreatText::realtimeAlert(m_realtimeThreats);
    DesktopNotifier::Notification notification;
    notification.title = alert.title;
    notification.body = alert.body;
    notification.icon = notificationIcon(QStringLiteral("security-low"), QStringLiteral(":/icons/result-threat.svg"));
    // Critique : reste affichée jusqu'à sa fermeture, même en mode « Ne pas déranger ».
    notification.urgency = DesktopNotifier::Urgency::Critical;
    notification.timeoutMsecs = 0;
    notification.actions = {{QStringLiteral("default"), tr("Afficher les détails")},
                            {QStringLiteral("details"), tr("Afficher les détails")}};
    // Pas d'aperçu du fichier dans la notification : pour le générer, le
    // bureau ouvrirait le fichier malveillant.
    if (m_realtimeThreats.size() == 1)
        notification.actions.append({QStringLiteral("folder"), tr("Ouvrir le dossier")});
    notify(kRealtimeKey, notification, StatusDisplay::threatIcon());
}

void TrayIcon::onNotificationAction(const QString &key, const QString &action, const QString &activationToken)
{
    if (action == QLatin1String("folder")) {
        if (!m_realtimeThreats.isEmpty())
            showInFileManager(m_realtimeThreats.first().path, activationToken);
        return;
    }
    // Sous Wayland, ce jeton autorise la fenêtre à prendre le focus : Qt le
    // lit dans cette variable quand la fenêtre demande à être activée.
    if (!activationToken.isEmpty())
        qputenv("XDG_ACTIVATION_TOKEN", activationToken.toUtf8());
    if (key == kRealtimeKey)
        emit showOnAccessRequested();
    else
        emit showWindowRequested();
}

void TrayIcon::notify(const QString &key, const DesktopNotifier::Notification &notification, const QIcon &fallbackIcon)
{
    m_fallbackIcons.insert(key, fallbackIcon);
    if (!m_notifier.show(key, notification))
        notifyFallback(key, notification);
}

void TrayIcon::notifyFallback(const QString &key, const DesktopNotifier::Notification &notification)
{
    m_lastMessageRealtime = key == kRealtimeKey;
    showMessage(notification.title, notification.body, m_fallbackIcons.value(key), kNotificationDuration);
}

void TrayIcon::updateState()
{
    const bool scanning = m_scans->isScanning();
    const ClamdWatcher::State clamdState = m_watcher->state();
    const QString clamdTitle = StatusDisplay::title(clamdState);
    const QString clamdDetails = StatusDisplay::details(*m_watcher);

    const QString onAccessTitle = StatusDisplay::onAccessTitle(m_onAccess->state());
    QString toolTip = clamdDetails.isEmpty() ? clamdTitle : clamdTitle + QLatin1Char('\n') + clamdDetails;
    toolTip += QLatin1Char('\n') + onAccessTitle;
    if (m_threatsPending) {
        setIcon(StatusDisplay::threatIcon());
        toolTip = m_threatSummary + QLatin1Char('\n') + toolTip;
    } else if (scanning) {
        setIcon(StatusDisplay::scanningIcon());
        toolTip = tr("Scan en cours : %1").arg(StatusDisplay::pathsText(m_scans->currentPaths()))
            + QLatin1Char('\n') + toolTip;
    } else {
        setIcon(StatusDisplay::icon(clamdState));
    }
    setToolTip(toolTip);

    m_statusAction->setIcon(StatusDisplay::icon(clamdState));
    m_statusAction->setText(clamdTitle);
    m_onAccessAction->setIcon(StatusDisplay::onAccessIcon(m_onAccess->state()));
    m_onAccessAction->setText(onAccessTitle);
    m_scanFolderAction->setEnabled(!scanning);
    m_stopAction->setVisible(scanning);
}
