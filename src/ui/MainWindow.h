#pragma once

#include <QMainWindow>

class ClamdWatcher;
class DashboardPage;
class HistoryPanel;
class OnAccessController;
class OnAccessPanel;
class QListWidget;
class QStackedWidget;
class ScanHistory;
class ScanManager;
class ScanPanel;

/**
 * Fenêtre principale : barre latérale de navigation et quatre pages,
 * « Accueil » (DashboardPage), « Analyse » (ScanPanel), « Protection en
 * temps réel » (OnAccessPanel) et « Historique » (HistoryPanel).
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
               QWidget *parent = nullptr);

    void showAndActivate();
    // Clic sur l'icône de notification : masque la fenêtre si elle est visible, l'affiche sinon.
    void toggleVisibility();
    // Affiche la fenêtre puis demande le dossier à scanner.
    void chooseFolderToScan();
    // Affiche la fenêtre et lance une analyse rapide.
    void startQuickScan();
    // Affiche la fenêtre sur la page « Protection en temps réel ».
    void showOnAccess();

signals:
    // Fenêtre affichée ou ramenée au premier plan : l'utilisateur voit les résultats.
    void windowActivated();
    // Les paramètres ont été modifiés et enregistrés (dialogue « Paramètres »).
    void settingsChanged();

protected:
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    enum Page { HomePage, ScanPage, OnAccessPage, HistoryPage };

    QWidget *createSidebar();
    void showPage(Page page);
    void updateNavigationIcons();
    void openSettings();
    void showAbout();

    ClamdWatcher *m_watcher;
    ScanManager *m_scans;
    OnAccessController *m_onAccess;
    DashboardPage *m_dashboard;
    ScanPanel *m_scanPanel;
    OnAccessPanel *m_onAccessPanel;
    HistoryPanel *m_historyPanel;
    QListWidget *m_navigation;
    QStackedWidget *m_pages;
    int m_realtimeThreats = 0; // détections en temps réel depuis le lancement, non effacées
};
