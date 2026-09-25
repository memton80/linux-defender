#pragma once

#include <QMainWindow>
#include <QSet>

class ClamdWatcher;
class DashboardPage;
class DiagnosticsPanel;
class HistoryPanel;
class OnAccessController;
class OnAccessPanel;
class PrivilegedHelper;
class Quarantine;
class QuarantinePanel;
class QListWidget;
class QStackedWidget;
class ScanHistory;
class ScanManager;
class ScanPanel;
class SystemDiagnostics;

/**
 * Fenêtre principale : barre latérale de navigation et six pages,
 * « Accueil » (DashboardPage), « Analyse » (ScanPanel), « Protection en
 * temps réel » (OnAccessPanel), « Quarantaine » (QuarantinePanel),
 * « Historique » (HistoryPanel) et « Diagnostic » (DiagnosticsPanel).
 *
 * Fermer la fenêtre la masque seulement : l'application continue de tourner
 * dans la zone de notification (voir main.cpp), sauf si les paramètres
 * demandent de quitter.
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(ClamdWatcher *watcher, ScanManager *scans, OnAccessController *onAccess, ScanHistory *history,
               SystemDiagnostics *diagnostics, PrivilegedHelper *helper, Quarantine *quarantine,
               QWidget *parent = nullptr);

    void showAndActivate();
    // Clic sur l'icône de notification : masque la fenêtre si elle est visible, l'affiche sinon.
    void toggleVisibility();
    // Affiche la fenêtre puis demande le dossier à scanner.
    void chooseFolderToScan();
    // Affiche la fenêtre et lance une analyse rapide.
    void startQuickScan();
    // Affiche la fenêtre et analyse ces fichiers et dossiers (menu de Dolphin, --scan).
    void scanPaths(const QStringList &paths);
    // Affiche la fenêtre sur la page « Protection en temps réel ».
    void showOnAccess();
    // Affiche la fenêtre sur la page « Diagnostic ».
    void showDiagnostics();

signals:
    // Fenêtre affichée ou ramenée au premier plan : l'utilisateur voit les résultats.
    void windowActivated();
    // Les paramètres ont été modifiés et enregistrés (dialogue « Paramètres »).
    void settingsChanged();
    // Une correction du diagnostic a modifié le système (configuration de
    // clamd, services) : états à relire.
    void systemChanged();

protected:
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    enum Page { HomePage, ScanPage, OnAccessPage, QuarantinePage, HistoryPage, DiagnosticsPage };

    QWidget *createSidebar();
    void showPage(Page page);
    void updateNavigationIcons();
    void openSettings();
    void showAbout();

    ClamdWatcher *m_watcher;
    ScanManager *m_scans;
    OnAccessController *m_onAccess;
    SystemDiagnostics *m_diagnostics;
    Quarantine *m_quarantine;
    DashboardPage *m_dashboard;
    ScanPanel *m_scanPanel;
    OnAccessPanel *m_onAccessPanel;
    QuarantinePanel *m_quarantinePanel;
    HistoryPanel *m_historyPanel;
    DiagnosticsPanel *m_diagnosticsPanel;
    QListWidget *m_navigation;
    QStackedWidget *m_pages;
    // Fichiers détectés en temps réel depuis le lancement, pas encore effacés
    // de la liste ni mis en quarantaine.
    QSet<QString> m_realtimeThreats;
    QStringList m_quarantineErrors; // échecs de la série d'opérations en cours
};
