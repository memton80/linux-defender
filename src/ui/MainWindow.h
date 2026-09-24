#pragma once

#include <QMainWindow>

class ClamdWatcher;
class QLabel;
class QPushButton;
class ScanManager;
class ScanPanel;

/**
 * Fenêtre principale : état de clamd, puis section « Scan » (ScanPanel).
 *
 * Fermer la fenêtre la masque seulement : l'application continue de tourner
 * dans la zone de notification (voir main.cpp).
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(ClamdWatcher *watcher, ScanManager *scans, QWidget *parent = nullptr);

    void showAndActivate();
    // Clic sur l'icône de notification : masque la fenêtre si elle est visible, l'affiche sinon.
    void toggleVisibility();
    // Affiche la fenêtre puis demande le dossier à scanner.
    void chooseFolderToScan();

signals:
    // Fenêtre affichée ou ramenée au premier plan : l'utilisateur voit les résultats.
    void windowActivated();
    // Les paramètres ont été modifiés et enregistrés (dialogue « Paramètres »).
    void settingsChanged();

protected:
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void updateStatus();
    void updateLastCheck();
    void openSettings();

    ClamdWatcher *m_watcher;
    ScanPanel *m_scanPanel;
    QLabel *m_icon;
    QLabel *m_title;
    QLabel *m_details;
    QLabel *m_socket;
    QLabel *m_lastCheck;
    QPushButton *m_checkButton;
};
