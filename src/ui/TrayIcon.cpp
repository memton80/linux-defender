#include "TrayIcon.h"

#include "StatusDisplay.h"
#include "core/ClamdWatcher.h"
#include "system/OnAccessController.h"

#include <QCoreApplication>
#include <QFileInfo>

namespace
{
constexpr int kNotificationDuration = 10000; // ms
constexpr int kMaxThreatsInNotification = 3;
}

TrayIcon::TrayIcon(ClamdWatcher *watcher, ScanManager *scans, OnAccessController *onAccess, QObject *parent)
    : QSystemTrayIcon(parent)
    , m_watcher(watcher)
    , m_scans(scans)
    , m_onAccess(onAccess)
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
    // Clic sur une notification : ouvrir la fenêtre pour voir le détail.
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
    updateState();
}

void TrayIcon::onScanStarted(const QStringList &paths, ScanManager::Origin origin)
{
    m_threats.clear();
    // Scan automatique : on prévient l'utilisateur, qui ne l'a pas demandé
    // (et ne pourra pas éjecter la clé tant que le scan lit ses fichiers).
    if (origin == ScanManager::Origin::Usb) {
        m_lastMessageRealtime = false;
        showMessage(tr("Scan de la clé USB"), tr("Analyse de %1 en cours…").arg(StatusDisplay::pathsText(paths)),
                    StatusDisplay::scanningIcon(), kNotificationDuration);
    }
    updateState();
}

void TrayIcon::onResults(const QList<ScanResult> &results)
{
    // Seules les premières menaces sont citées dans la notification : inutile de garder les autres.
    for (const ScanResult &result : results) {
        if (result.status == ScanResult::Status::Infected && m_threats.size() < kMaxThreatsInNotification)
            m_threats.append(tr("%1 : %2").arg(QFileInfo(result.path).fileName(), result.detail));
    }
}

void TrayIcon::onScanFinished(const ScanSummary &summary, ScanManager::Origin origin)
{
    const QString paths = StatusDisplay::pathsText(summary.paths);

    if (summary.infected > 0) {
        QStringList lines = m_threats;
        const qint64 others = summary.infected - m_threats.size();
        if (others == 1)
            lines.append(tr("… et 1 autre"));
        else if (others > 1)
            lines.append(tr("… et %1 autres").arg(others));
        m_threatsPending = true;
        m_threatSummary = tr("Menaces détectées dans %1").arg(paths);
        m_lastMessageRealtime = false;
        showMessage(tr("Menaces détectées !"),
                    tr("%1\n%2").arg(StatusDisplay::summaryText(summary), lines.join(QLatin1Char('\n'))),
                    StatusDisplay::threatIcon(), kNotificationDuration);
    } else if (!summary.fatalError.isEmpty()) {
        m_lastMessageRealtime = false;
        showMessage(tr("Scan impossible"), summary.fatalError,
                    StatusDisplay::resultIcon(ScanResult::Status::Error), kNotificationDuration);
    } else if (origin == ScanManager::Origin::Usb && !summary.cancelled) {
        m_lastMessageRealtime = false;
        showMessage(tr("Clé USB analysée"), tr("%1\n%2").arg(paths, StatusDisplay::summaryText(summary)),
                    StatusDisplay::resultIcon(ScanResult::Status::Clean), kNotificationDuration);
    }
    m_threats.clear();
    updateState();
}

void TrayIcon::onRealtimeThreat(const OnAccessDetection &detection)
{
    m_threatsPending = true;
    m_threatSummary = tr("Menace détectée en temps réel : %1").arg(detection.path);
    m_lastMessageRealtime = true;
    showMessage(tr("Menace détectée en temps réel !"),
                tr("Fichier : %1\nMenace : %2\nLe fichier n'a été ni supprimé ni déplacé.")
                    .arg(detection.path, detection.threat),
                StatusDisplay::threatIcon(), kNotificationDuration);
    updateState();
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
