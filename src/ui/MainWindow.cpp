#include "MainWindow.h"

#include "OnAccessPanel.h"
#include "ScanPanel.h"
#include "SettingsDialog.h"
#include "StatusDisplay.h"
#include "core/ClamdWatcher.h"
#include "system/OnAccessController.h"

#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QStyle>
#include <QTabWidget>
#include <QVBoxLayout>

MainWindow::MainWindow(ClamdWatcher *watcher, ScanManager *scans, OnAccessController *onAccess, QWidget *parent)
    : QMainWindow(parent)
    , m_watcher(watcher)
    , m_onAccess(onAccess)
    , m_scanPanel(new ScanPanel(scans))
    , m_onAccessPanel(new OnAccessPanel(onAccess))
    , m_tabs(new QTabWidget)
{
    // En-tête : icône d'état, titre et détails (version ou message d'erreur).
    m_icon = new QLabel;
    m_title = new QLabel;
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.2);
    m_title->setFont(titleFont);

    // Pas de retour à la ligne automatique : les messages contiennent déjà leurs
    // sauts de ligne, et un QLabel à retour automatique ne fait pas grandir la
    // fenêtre (la fin du message serait coupée).
    m_details = new QLabel;
    m_details->setTextFormat(Qt::PlainText);
    // Sélectionnable, pour pouvoir copier la commande proposée en cas d'erreur.
    m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *texts = new QVBoxLayout;
    texts->addWidget(m_title);
    texts->addWidget(m_details);

    auto *header = new QHBoxLayout;
    header->addWidget(m_icon, 0, Qt::AlignTop);
    header->addLayout(texts, 1);

    // Informations techniques.
    m_socket = new QLabel;
    m_socket->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_lastCheck = new QLabel;

    auto *info = new QFormLayout;
    info->addRow(tr("Socket :"), m_socket);
    info->addRow(tr("Dernière vérification :"), m_lastCheck);

    m_checkButton = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), tr("Vérifier maintenant"));
    connect(m_checkButton, &QPushButton::clicked, this, [this] {
        m_checkButton->setEnabled(false); // réactivé à la fin de la vérification
        m_watcher->checkNow();
    });

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(m_checkButton);

    auto *statusBox = new QGroupBox(tr("État de clamd"));
    auto *statusLayout = new QVBoxLayout(statusBox);
    statusLayout->addLayout(header);
    statusLayout->addLayout(info);
    statusLayout->addLayout(buttons);

    auto *settingsButton = new QPushButton(QIcon::fromTheme(QStringLiteral("configure")), tr("Paramètres…"));
    connect(settingsButton, &QPushButton::clicked, this, &MainWindow::openSettings);
    auto *bottom = new QHBoxLayout;
    bottom->addStretch();
    bottom->addWidget(settingsButton);

    m_tabs->addTab(m_scanPanel, tr("Scan"));
    m_tabs->addTab(m_onAccessPanel, tr("Protection en temps réel"));
    // Une menace détectée en temps réel est l'information la plus urgente :
    // la fenêtre s'ouvrira directement sur cet onglet.
    connect(m_onAccess, &OnAccessController::threatDetected, this, [this] {
        m_tabs->setCurrentWidget(m_onAccessPanel);
    });

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->addWidget(statusBox);
    layout->addWidget(m_tabs, 1);
    layout->addLayout(bottom);
    setCentralWidget(central);

    connect(m_watcher, &ClamdWatcher::statusChanged, this, &MainWindow::updateStatus);
    connect(m_watcher, &ClamdWatcher::checkFinished, this, &MainWindow::updateLastCheck);
    updateStatus();
    updateLastCheck();

    // Taille initiale seulement. Pas de setMinimumWidth() : il empêcherait la
    // fenêtre de s'élargir d'elle-même pour afficher un long message d'erreur.
    resize(sizeHint().expandedTo(QSize(760, 640)));
}

void MainWindow::showAndActivate()
{
    show();
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    raise();
    activateWindow();
}

void MainWindow::toggleVisibility()
{
    if (isVisible() && !isMinimized())
        hide();
    else
        showAndActivate();
}

void MainWindow::chooseFolderToScan()
{
    showAndActivate();
    m_scanPanel->chooseFolder();
}

void MainWindow::showOnAccess()
{
    m_tabs->setCurrentWidget(m_onAccessPanel);
    showAndActivate();
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    // États à jour dès que la fenêtre apparaît.
    m_watcher->checkNow();
    m_onAccess->refresh();
    emit windowActivated();
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::ActivationChange && isActiveWindow())
        emit windowActivated();
}

void MainWindow::openSettings()
{
    SettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted)
        emit settingsChanged();
}

void MainWindow::updateStatus()
{
    const ClamdWatcher::State state = m_watcher->state();
    // Taille fournie par le style (Breeze), comme pour les boîtes de dialogue.
    const int size = style()->pixelMetric(QStyle::PM_MessageBoxIconSize, nullptr, this);
    m_icon->setPixmap(StatusDisplay::icon(state).pixmap(QSize(size, size), devicePixelRatioF()));
    m_title->setText(StatusDisplay::title(state));

    const QString details = StatusDisplay::details(*m_watcher);
    m_details->setText(details);
    m_details->setVisible(!details.isEmpty());
}

void MainWindow::updateLastCheck()
{
    m_socket->setText(m_watcher->socketPath());
    const QDateTime lastCheck = m_watcher->lastCheck();
    m_lastCheck->setText(lastCheck.isValid() ? QLocale().toString(lastCheck.time(), QLocale::LongFormat)
                                             : tr("en cours…"));
    m_checkButton->setEnabled(true);
}
