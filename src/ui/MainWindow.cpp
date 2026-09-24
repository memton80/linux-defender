#include "MainWindow.h"

#include "DashboardPage.h"
#include "HistoryPanel.h"
#include "OnAccessPanel.h"
#include "ScanPanel.h"
#include "SettingsDialog.h"
#include "StatusDisplay.h"
#include "Widgets.h"
#include "core/ClamdWatcher.h"
#include "core/Settings.h"
#include "system/OnAccessController.h"

#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QStackedWidget>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QToolButton>
#include <QVBoxLayout>

MainWindow::MainWindow(ClamdWatcher *watcher, ScanManager *scans, OnAccessController *onAccess, ScanHistory *history,
                       QWidget *parent)
    : QMainWindow(parent)
    , m_watcher(watcher)
    , m_scans(scans)
    , m_onAccess(onAccess)
    , m_dashboard(new DashboardPage(watcher, scans, onAccess, history))
    , m_scanPanel(new ScanPanel(scans, history))
    , m_onAccessPanel(new OnAccessPanel(onAccess))
    , m_historyPanel(new HistoryPanel(history))
    , m_pages(new QStackedWidget)
{
    // Pages, dans l'ordre de l'énumération Page (et de la barre latérale).
    m_pages->addWidget(m_dashboard);
    m_pages->addWidget(m_scanPanel);
    m_pages->addWidget(m_onAccessPanel);
    m_pages->addWidget(m_historyPanel);

    // Barre latérale | séparateur | page courante.
    auto *separator = new QFrame;
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Sunken);
    auto *central = new QWidget;
    auto *layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(createSidebar());
    layout->addWidget(separator);
    layout->addWidget(m_pages, 1);
    setCentralWidget(central);

    // Accueil : chaque action mène à la page qui montre son résultat.
    connect(m_dashboard, &DashboardPage::checkRequested, m_watcher, &ClamdWatcher::checkNow);
    connect(m_dashboard, &DashboardPage::quickScanRequested, m_scanPanel, &ScanPanel::startQuickScan);
    connect(m_dashboard, &DashboardPage::fullScanRequested, m_scanPanel, &ScanPanel::startFullScan);
    connect(m_dashboard, &DashboardPage::folderScanRequested, m_scanPanel, &ScanPanel::chooseFolder);
    connect(m_dashboard, &DashboardPage::filesScanRequested, m_scanPanel, &ScanPanel::chooseFiles);
    connect(m_dashboard, &DashboardPage::showScanRequested, this, [this] { showPage(ScanPage); });
    connect(m_dashboard, &DashboardPage::showOnAccessRequested, this, [this] { showPage(OnAccessPage); });
    connect(m_dashboard, &DashboardPage::showHistoryRequested, this, [this] {
        m_historyPanel->selectLatest();
        showPage(HistoryPage);
    });
    connect(m_dashboard, &DashboardPage::settingsRequested, this, &MainWindow::openSettings);
    connect(m_dashboard, &DashboardPage::levelChanged, this, &MainWindow::updateNavigationIcons);

    // Analyse lancée depuis la fenêtre ou l'icône : sa progression est sur la
    // page « Analyse ». Une clé USB ne change pas la page que l'utilisateur lit.
    connect(m_scans, &ScanManager::scanStarted, this, [this](const QStringList &, ScanManager::Origin origin) {
        if (origin != ScanManager::Origin::Usb)
            showPage(ScanPage);
        updateNavigationIcons();
    });
    connect(m_scans, &ScanManager::scanFinished, this, &MainWindow::updateNavigationIcons);

    // Une menace détectée en temps réel est l'information la plus urgente :
    // la fenêtre s'ouvrira directement sur cette page.
    connect(m_onAccess, &OnAccessController::stateChanged, this, &MainWindow::updateNavigationIcons);
    connect(m_onAccess, &OnAccessController::threatDetected, this, [this] {
        m_dashboard->setRealtimeThreats(++m_realtimeThreats);
        showPage(OnAccessPage);
        updateNavigationIcons();
    });
    connect(m_onAccessPanel, &OnAccessPanel::detectionsCleared, this, [this] {
        m_realtimeThreats = 0;
        m_dashboard->setRealtimeThreats(0);
        updateNavigationIcons();
    });

    updateNavigationIcons();
    showPage(HomePage);

    // Taille initiale seulement. Pas de setMinimumWidth() : il empêcherait la
    // fenêtre de s'élargir d'elle-même pour afficher un long message d'erreur.
    resize(sizeHint().expandedTo(QSize(1000, 700)));
}

QWidget *MainWindow::createSidebar()
{
    // En-tête : icône et nom de l'application.
    auto *appIcon = new QLabel;
    Widgets::setIcon(appIcon, QApplication::windowIcon(), style()->pixelMetric(QStyle::PM_LargeIconSize, nullptr, this));
    auto *name = new QLabel(QApplication::applicationDisplayName());
    name->setFont(Widgets::scaledFont(name->font(), 1.2, true));
    auto *tagline = new QLabel(tr("Antivirus ClamAV"));
    Widgets::setSecondary(tagline);
    auto *names = new QVBoxLayout;
    names->setSpacing(0);
    names->addWidget(name);
    names->addWidget(tagline);
    auto *header = new QHBoxLayout;
    header->addWidget(appIcon);
    header->addLayout(names, 1);

    // Navigation entre les pages.
    m_navigation = new QListWidget;
    m_navigation->setFrameShape(QFrame::NoFrame);
    m_navigation->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    const int iconSize = style()->pixelMetric(QStyle::PM_ToolBarIconSize, nullptr, this);
    m_navigation->setIconSize(QSize(iconSize, iconSize));
    const int rowHeight = qMax(iconSize, fontMetrics().height()) + fontMetrics().height();
    for (const QString &text : {tr("Accueil"), tr("Analyse"), tr("Protection en temps réel"), tr("Historique")}) {
        auto *item = new QListWidgetItem(text, m_navigation);
        item->setSizeHint(QSize(0, rowHeight));
    }
    connect(m_navigation, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);

    // Actions générales, en bas, alignées sur les pages.
    const auto sidebarButton = [this, iconSize](const char *icon, const QString &text) {
        auto *button = new QToolButton;
        button->setIcon(QIcon::fromTheme(QString::fromLatin1(icon)));
        button->setIconSize(QSize(iconSize, iconSize));
        button->setText(text);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setAutoRaise(true);
        return button;
    };
    QToolButton *settingsButton = sidebarButton("configure", tr("Paramètres"));
    connect(settingsButton, &QToolButton::clicked, this, &MainWindow::openSettings);
    QToolButton *aboutButton = sidebarButton("help-about", tr("À propos"));
    connect(aboutButton, &QToolButton::clicked, this, &MainWindow::showAbout);

    auto *sidebar = new QWidget;
    // Fond des listes (plus clair ou plus sombre que la fenêtre selon le
    // thème) : la barre se distingue du contenu, comme dans Dolphin.
    sidebar->setBackgroundRole(QPalette::Base);
    sidebar->setAutoFillBackground(true);
    auto *layout = new QVBoxLayout(sidebar);
    layout->addLayout(header);
    layout->addSpacing(style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing));
    layout->addWidget(m_navigation, 1);
    layout->addWidget(settingsButton, 0, Qt::AlignLeft);
    layout->addWidget(aboutButton, 0, Qt::AlignLeft);

    // Largeur : le plus long des libellés, icône et marges du style comprises.
    int textWidth = 0;
    for (int row = 0; row < m_navigation->count(); ++row)
        textWidth = qMax(textWidth, fontMetrics().horizontalAdvance(m_navigation->item(row)->text()));
    const int itemWidth = iconSize + textWidth + 4 * fontMetrics().averageCharWidth();
    const QMargins margins = layout->contentsMargins();
    sidebar->setFixedWidth(qMax(itemWidth, header->sizeHint().width()) + margins.left() + margins.right());
    return sidebar;
}

void MainWindow::showPage(Page page)
{
    m_navigation->setCurrentRow(page);
}

void MainWindow::updateNavigationIcons()
{
    m_navigation->item(HomePage)->setIcon(StatusDisplay::levelIcon(m_dashboard->level()));

    m_navigation->item(ScanPage)->setIcon(m_scans->isScanning()
                                              ? StatusDisplay::scanningIcon()
                                              : QIcon::fromTheme(QStringLiteral("system-search"), StatusDisplay::scanningIcon()));
    m_navigation->item(ScanPage)->setToolTip(m_scans->isScanning() ? tr("Analyse en cours") : QString());

    const OnAccessController::State onAccess = m_onAccess->state();
    m_navigation->item(OnAccessPage)->setIcon(m_realtimeThreats > 0 ? StatusDisplay::threatIcon()
                                                                    : StatusDisplay::onAccessIcon(onAccess));
    m_navigation->item(OnAccessPage)->setToolTip(StatusDisplay::onAccessTitle(onAccess));

    m_navigation->item(HistoryPage)->setIcon(QIcon::fromTheme(
        QStringLiteral("view-history"), QIcon::fromTheme(QStringLiteral("document-open-recent"),
                                                         StatusDisplay::levelIcon(StatusDisplay::Level::Neutral))));
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
    showPage(ScanPage);
    m_scanPanel->chooseFolder();
}

void MainWindow::startQuickScan()
{
    showAndActivate();
    m_scanPanel->startQuickScan();
}

void MainWindow::showOnAccess()
{
    showPage(OnAccessPage);
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

void MainWindow::closeEvent(QCloseEvent *event)
{
    QMainWindow::closeEvent(event);
    // La fenêtre est masquée. Sans zone de notification, Qt quitte déjà
    // l'application (voir main.cpp) ; avec, seulement si l'utilisateur le demande.
    if (event->isAccepted() && QSystemTrayIcon::isSystemTrayAvailable() && !Settings::closeToTray())
        QCoreApplication::quit();
}

void MainWindow::openSettings()
{
    SettingsDialog dialog(this);
    connect(&dialog, &SettingsDialog::applied, this, [this] {
        emit settingsChanged();
        m_dashboard->refresh();
    });
    dialog.exec();
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, tr("À propos de Linux Defender"),
        tr("<h3>Linux Defender %1</h3>"
           "<p>Interface légère pour l'antivirus ClamAV : analyses à la demande, clés USB, "
           "protection en temps réel (clamonacc).</p>"
           "<p>Qt %2 · <a href=\"%3\">%3</a></p>")
            .arg(QApplication::applicationVersion(), QString::fromLatin1(qVersion()),
                 QStringLiteral("https://github.com/memton80/linux-defender")));
}
