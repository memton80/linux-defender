#pragma once

#include "core/ScanManager.h"
#include "system/OnAccessLog.h"

#include <QMenu>
#include <QSystemTrayIcon>

class ClamdWatcher;
class OnAccessController;
class QAction;

/**
 * Icône dans la zone de notification. Sous Plasma, Qt passe par le protocole
 * StatusNotifierItem (D-Bus) : l'icône s'intègre comme celles des applications
 * KDE, et showMessage() produit une vraie notification Plasma.
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
    void updateState();

    ClamdWatcher *m_watcher;
    ScanManager *m_scans;
    OnAccessController *m_onAccess;
    QMenu m_menu;
    QAction *m_statusAction;
    QAction *m_onAccessAction;
    QAction *m_scanFolderAction;
    QAction *m_stopAction;

    QStringList m_threats;      // premières menaces du scan en cours, pour la notification
    bool m_threatsPending = false;
    QString m_threatSummary;    // infobulle tant que les menaces n'ont pas été consultées
    bool m_lastMessageRealtime = false; // dernière notification : détection en temps réel ?
};
