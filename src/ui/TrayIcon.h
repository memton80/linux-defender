#pragma once

#include <QMenu>
#include <QSystemTrayIcon>

class ClamdWatcher;
class QAction;

/**
 * Icône dans la zone de notification. Sous Plasma, Qt passe par le protocole
 * StatusNotifierItem (D-Bus) : l'icône s'intègre comme celles des applications KDE.
 *
 * Elle affiche l'état de clamd (icône et infobulle) et propose un menu minimal.
 * Elle ne connaît pas la fenêtre : elle émet des signaux, reliés dans main.cpp.
 */
class TrayIcon : public QSystemTrayIcon
{
    Q_OBJECT

public:
    explicit TrayIcon(ClamdWatcher *watcher, QObject *parent = nullptr);

signals:
    void showWindowRequested();
    void toggleWindowRequested();

private:
    void updateStatus();

    ClamdWatcher *m_watcher;
    QMenu m_menu;
    QAction *m_statusAction;
};
