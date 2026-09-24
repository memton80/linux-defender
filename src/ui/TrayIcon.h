#pragma once

#include "core/ScanManager.h"
#include "core/ThreatText.h"
#include "system/DesktopNotifier.h"
#include "system/OnAccessLog.h"

#include <QHash>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>

class ClamdWatcher;
class OnAccessController;
class QAction;

/**
 * Icône dans la zone de notification. Sous Plasma, Qt passe par le protocole
 * StatusNotifierItem (D-Bus) : l'icône s'intègre comme celles des applications
 * KDE.
 *
 * Les notifications passent par DesktopNotifier (boutons, alerte critique qui
 * reste affichée, icônes du thème). Sans service de notification, elles se
 * rabattent sur QSystemTrayIcon::showMessage().
 *
 * L'icône montre, par ordre de priorité : des menaces détectées (scan ou temps
 * réel) et pas encore consultées, un scan en cours, puis l'état de clamd.
 * Elle ne connaît pas la fenêtre : elle émet des signaux, reliés dans main.cpp.
 */
class TrayIcon : public QSystemTrayIcon
{
    Q_OBJECT

public:
    TrayIcon(ClamdWatcher *watcher, ScanManager *scans, OnAccessController *onAccess, QObject *parent = nullptr);

    // L'utilisateur a vu les résultats (fenêtre ouverte) : l'icône « menace » disparaît.
    void acknowledgeThreats();

signals:
    void showWindowRequested();
    void toggleWindowRequested();
    void scanFolderRequested();
    // Clic sur la notification d'une détection en temps réel.
    void showOnAccessRequested();

private:
    void onScanStarted(const QStringList &paths, ScanManager::Origin origin);
    void onResults(const QList<ScanResult> &results);
    void onScanFinished(const ScanSummary &summary, ScanManager::Origin origin);
    void onRealtimeThreat(const OnAccessDetection &detection);
    void showRealtimeAlert();
    void onNotificationAction(const QString &key, const QString &action, const QString &activationToken);
    // Affiche une notification ; `fallbackIcon` sert si le bureau n'a pas de
    // service de notification.
    void notify(const QString &key, const DesktopNotifier::Notification &notification, const QIcon &fallbackIcon);
    void notifyFallback(const QString &key, const DesktopNotifier::Notification &notification);
    void updateState();

    ClamdWatcher *m_watcher;
    ScanManager *m_scans;
    OnAccessController *m_onAccess;
    QMenu m_menu;
    QAction *m_statusAction;
    QAction *m_onAccessAction;
    QAction *m_scanFolderAction;
    QAction *m_stopAction;

    DesktopNotifier m_notifier;
    QHash<QString, QIcon> m_fallbackIcons; // par clé de notification
    QList<ThreatText::Threat> m_scanThreats;     // premières menaces du scan en cours
    QList<ThreatText::Threat> m_realtimeThreats; // détections de l'alerte affichée, pas encore consultées
    bool m_threatsPending = false;
    QString m_threatSummary;    // infobulle tant que les menaces n'ont pas été consultées
    bool m_lastMessageRealtime = false; // dernière notification : détection en temps réel ?
};
