#include "TrayIcon.h"

#include "StatusDisplay.h"
#include "core/ClamdWatcher.h"

#include <QCoreApplication>

TrayIcon::TrayIcon(ClamdWatcher *watcher, QObject *parent)
    : QSystemTrayIcon(parent)
    , m_watcher(watcher)
{
    // Première ligne : l'état de clamd, pour information (non cliquable).
    m_statusAction = m_menu.addAction(QString());
    m_statusAction->setEnabled(false);

    QAction *check = m_menu.addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), tr("Vérifier l'état de clamd"));
    connect(check, &QAction::triggered, m_watcher, &ClamdWatcher::checkNow);

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

    connect(m_watcher, &ClamdWatcher::statusChanged, this, &TrayIcon::updateStatus);
    updateStatus();
}

void TrayIcon::updateStatus()
{
    const ClamdWatcher::State state = m_watcher->state();
    const QIcon icon = StatusDisplay::icon(state);
    const QString title = StatusDisplay::title(state);
    const QString details = StatusDisplay::details(*m_watcher);

    setIcon(icon);
    setToolTip(details.isEmpty() ? title : title + QLatin1Char('\n') + details);
    m_statusAction->setIcon(icon);
    m_statusAction->setText(title);
}
