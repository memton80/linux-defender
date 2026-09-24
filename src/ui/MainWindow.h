#pragma once

#include <QMainWindow>

class ClamdWatcher;
class QLabel;
class QPushButton;

/**
 * Fenêtre principale. Pour l'instant, elle n'affiche que l'état de clamd ;
 * les sections suivantes (scan, etc.) viendront s'ajouter en dessous.
 *
 * Fermer la fenêtre la masque seulement : l'application continue de tourner
 * dans la zone de notification (voir main.cpp).
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(ClamdWatcher *watcher, QWidget *parent = nullptr);

    void showAndActivate();
    // Clic sur l'icône de notification : masque la fenêtre si elle est visible, l'affiche sinon.
    void toggleVisibility();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void updateStatus();
    void updateLastCheck();

    ClamdWatcher *m_watcher;
    QLabel *m_icon;
    QLabel *m_title;
    QLabel *m_details;
    QLabel *m_lastCheck;
    QPushButton *m_checkButton;
};
