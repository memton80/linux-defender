#include "SettingsDialog.h"

#include "StatusDisplay.h"
#include "Widgets.h"
#include "core/ClamdClient.h"
#include "core/ClamdConfig.h"
#include "core/ScanHistory.h"
#include "core/Settings.h"
#include "system/Autostart.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QVBoxLayout>

namespace
{
// Explication sous un réglage : texte secondaire, avec retour à la ligne.
QLabel *note(const QString &text)
{
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    Widgets::setSecondary(label);
    return label;
}

QGroupBox *group(const QString &title, QLayout *layout)
{
    auto *box = new QGroupBox(title);
    box->setLayout(layout);
    return box;
}
}

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_testClient(new ClamdClient(this))
{
    setWindowTitle(tr("Paramètres"));

    m_pageList = new QListWidget;
    m_pages = new QStackedWidget;
    // Même présentation que la barre latérale de la fenêtre principale.
    const int iconSize = style()->pixelMetric(QStyle::PM_ToolBarIconSize, nullptr, this);
    m_pageList->setIconSize(QSize(iconSize, iconSize));
    connect(m_pageList, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);

    const QIcon appIcon = QApplication::windowIcon();
    addPage(createGeneralPage(), tr("Général"), tr("Démarrage, fenêtre et historique."),
            QIcon::fromTheme(QStringLiteral("preferences-system"), appIcon));
    addPage(createScanPage(), tr("Analyse"), tr("Dossiers de l'analyse rapide, exclusions et options des analyses."),
            QIcon::fromTheme(QStringLiteral("system-search"), StatusDisplay::scanningIcon()));
    addPage(createUsbPage(), tr("Clés USB"), tr("Analyse des clés USB et des cartes mémoire."),
            QIcon::fromTheme(QStringLiteral("drive-removable-media-usb"),
                             QIcon::fromTheme(QStringLiteral("drive-removable-media"), appIcon)));
    addPage(createNotificationsPage(), tr("Notifications"), tr("Ce qui vous est signalé par une notification."),
            QIcon::fromTheme(QStringLiteral("preferences-desktop-notification"), appIcon));
    addPage(createClamdPage(), tr("clamd"), tr("Connexion au démon de ClamAV et surveillance de son état."),
            QIcon::fromTheme(QStringLiteral("network-server-database"), StatusDisplay::icon(ClamdWatcher::State::Connected)));
    const int rowHeight = qMax(iconSize, fontMetrics().height()) + fontMetrics().height();
    int textWidth = 0;
    for (int row = 0; row < m_pageList->count(); ++row) {
        m_pageList->item(row)->setSizeHint(QSize(0, rowHeight));
        textWidth = qMax(textWidth, fontMetrics().horizontalAdvance(m_pageList->item(row)->text()));
    }
    m_pageList->setFixedWidth(iconSize + textWidth + 4 * fontMetrics().averageCharWidth() + 2 * m_pageList->frameWidth());
    m_pageList->setCurrentRow(0);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel
                                     | QDialogButtonBox::RestoreDefaults);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    connect(m_buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &SettingsDialog::apply);
    connect(m_buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this,
            &SettingsDialog::loadDefaults);

    auto *pagesLayout = new QHBoxLayout;
    pagesLayout->addWidget(m_pageList);
    pagesLayout->addWidget(m_pages, 1);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(pagesLayout, 1);
    layout->addWidget(m_buttons);

    load();
    watchChanges();
    setModified(false);
    resize(sizeHint().expandedTo(QSize(800, 640)));
}

void SettingsDialog::addPage(QWidget *content, const QString &name, const QString &description, const QIcon &icon)
{
    auto *title = new QLabel(name);
    title->setFont(Widgets::scaledFont(title->font(), 1.3, true));
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(title);
    layout->addWidget(note(description));
    layout->addSpacing(style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing));
    layout->addWidget(content, 1);

    // Défilement si l'écran est petit.
    auto *scroll = new QScrollArea;
    scroll->setWidget(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_pages->addWidget(scroll);
    new QListWidgetItem(icon, name, m_pageList);
}

QWidget *SettingsDialog::createGeneralPage()
{
    m_autostart = new QCheckBox(tr("Lancer Linux Defender à l'ouverture de la session"));
    auto *startup = new QVBoxLayout;
    startup->addWidget(m_autostart);
    startup->addWidget(note(tr("L'application démarre dans la zone de notification, sans ouvrir sa fenêtre : "
                               "clés USB et protection en temps réel sont suivies dès la connexion.")));

    m_closeToTray = new QCheckBox(tr("Garder l'application dans la zone de notification quand la fenêtre est fermée"));
    auto *window = new QVBoxLayout;
    window->addWidget(m_closeToTray);
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        window->addWidget(note(tr("Sinon, fermer la fenêtre quitte l'application : plus d'analyse automatique "
                                  "des clés USB ni de notifications.")));
    } else {
        m_closeToTray->setEnabled(false);
        window->addWidget(note(tr("Aucune zone de notification n'est disponible : fermer la fenêtre quitte "
                                  "toujours l'application.")));
    }

    m_historyMax = new QSpinBox;
    m_historyMax->setRange(0, 10000);
    m_historyMax->setSpecialValueText(tr("Aucune (historique désactivé)"));
    m_historyMax->setSuffix(tr(" analyses"));
    auto *historyForm = new QFormLayout;
    historyForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    historyForm->addRow(tr("Analyses conservées :"), m_historyMax);
    auto *history = new QVBoxLayout;
    history->addLayout(historyForm);
    history->addWidget(note(tr("Les plus anciennes sont oubliées. Fichier : %1").arg(ScanHistory::defaultFilePath())));

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(group(tr("Démarrage"), startup));
    layout->addWidget(group(tr("Fenêtre"), window));
    layout->addWidget(group(tr("Historique"), history));
    layout->addStretch();
    return page;
}

QWidget *SettingsDialog::createScanPage()
{
    m_quickScanPaths = new PathListEdit(PathListEdit::Mode::Folders);
    auto *quick = new QVBoxLayout;
    quick->addWidget(note(tr("Dossiers analysés par « Analyse rapide » : ceux où arrivent les nouveaux fichiers. "
                             "Liste vide : %1.")
                              .arg(StatusDisplay::targetText(ScanManager::Origin::Quick,
                                                             Settings::Defaults::quickScanPaths()))));
    quick->addWidget(m_quickScanPaths);

    m_excludedPaths = new PathListEdit(PathListEdit::Mode::FoldersAndFiles);
    auto *exclusions = new QVBoxLayout;
    exclusions->addWidget(note(tr("Ignorés, avec tout leur contenu, quand ils se trouvent dans un dossier analysé "
                                  "(analyses et clés USB). Utile pour les machines virtuelles, les sauvegardes ou "
                                  "un faux positif. Choisis directement, ils sont quand même analysés.")));
    exclusions->addWidget(m_excludedPaths);

    m_scanHidden = new QCheckBox(tr("Analyser les fichiers et dossiers cachés (nom commençant par un point)"));
    m_limitFileSize = new QCheckBox(tr("Ignorer les fichiers de plus de"));
    m_maxFileSize = new QSpinBox;
    m_maxFileSize->setRange(1, 1024 * 1024);
    m_maxFileSize->setSuffix(tr(" Mo"));
    auto *sizeRow = new QHBoxLayout;
    sizeRow->addWidget(m_limitFileSize);
    sizeRow->addWidget(m_maxFileSize);
    sizeRow->addStretch();
    auto *options = new QVBoxLayout;
    options->addWidget(m_scanHidden);
    options->addLayout(sizeRow);
    // Limite de clamd lui-même : au-delà, il répond « OK » sans lire le fichier.
    const ClamdConfig clamdConfig = ClamdConfig::forSocket(Settings::effectiveSocketPath());
    const QString clamdLimit = clamdConfig.maxFileSize > 0
        ? tr("%1 Mo").arg(QLocale().toString(double(clamdConfig.unscannedAbove()) / (1024 * 1024), 'g', 4))
        : tr("aucune (2 Go au plus)");
    options->addWidget(note(tr("Les fichiers ignorés sont comptés dans le bilan. clamd a aussi sa propre limite "
                               "(MaxFileSize, actuellement : %1, lue dans %2) : au-delà, il ne lit pas le fichier, "
                               "qui est alors signalé « non analysé ».")
                                .arg(clamdLimit, clamdConfig.path.isEmpty() ? tr("les valeurs par défaut de clamd")
                                                                            : clamdConfig.path)));

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(group(tr("Analyse rapide"), quick), 1);
    layout->addWidget(group(tr("Exclusions"), exclusions), 1);
    layout->addWidget(group(tr("Options"), options));
    return page;
}

QWidget *SettingsDialog::createUsbPage()
{
    m_usbAutoScan = new QCheckBox(tr("Analyser automatiquement les clés USB et cartes mémoire au montage"));
    m_usbNotify = new QCheckBox(tr("Notifier le début et la fin de l'analyse d'une clé"));
    auto *usb = new QVBoxLayout;
    usb->addWidget(m_usbAutoScan);
    usb->addWidget(m_usbNotify);
    usb->addWidget(note(tr("Les menaces trouvées sur une clé sont toujours notifiées.")));

    auto *help = new QVBoxLayout;
    help->addWidget(note(tr("Sous Plasma, une clé est montée quand vous l'ouvrez (Dolphin, notification "
                            "« Périphériques »), ou dès le branchement si le montage automatique est activé "
                            "(Configuration du système → Disques et caméras → Montage automatique des "
                            "périphériques).")));
    help->addWidget(note(tr("Tant qu'une analyse lit la clé, celle-ci ne peut pas être éjectée : arrêtez "
                            "l'analyse depuis la fenêtre ou le menu de l'icône si besoin.")));

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(group(tr("Clés USB et cartes mémoire"), usb));
    layout->addWidget(group(tr("Bon à savoir"), help));
    layout->addStretch();
    return page;
}

QWidget *SettingsDialog::createNotificationsPage()
{
    m_notifyScanFinished = new QCheckBox(tr("Fin d'une analyse sans menace, quand la fenêtre n'est pas au premier plan"));
    auto *scans = new QVBoxLayout;
    scans->addWidget(m_notifyScanFinished);
    scans->addWidget(note(tr("Les menaces trouvées par une analyse sont toujours notifiées.")));

    m_notifyRealtime = new QCheckBox(tr("Menace détectée par la protection en temps réel"));
    m_notifyClamdLost = new QCheckBox(tr("clamd ne répond plus"));
    m_notifySignatures = new QCheckBox(tr("Signatures obsolètes (âge réglable dans la page « clamd »)"));
    auto *protection = new QVBoxLayout;
    protection->addWidget(m_notifyRealtime);
    protection->addWidget(m_notifyClamdLost);
    protection->addWidget(m_notifySignatures);
    protection->addWidget(note(tr("Sans notification, l'icône de la zone de notification et l'accueil de la "
                                  "fenêtre signalent toujours ces problèmes.")));

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(group(tr("Analyses"), scans));
    layout->addWidget(group(tr("Protection"), protection));
    layout->addStretch();
    return page;
}

QWidget *SettingsDialog::createClamdPage()
{
    // Vide = détection automatique ; le chemin détecté est affiché en indication.
    m_socketPath = new QLineEdit;
    m_socketPath->setPlaceholderText(tr("Automatique : %1").arg(ClamdClient::detectSocketPath()));
    m_socketPath->setClearButtonEnabled(true);
    auto *testButton = new QPushButton(QIcon::fromTheme(QStringLiteral("network-connect")), tr("Tester"));
    testButton->setToolTip(tr("Vérifier que clamd répond sur ce socket"));
    connect(testButton, &QPushButton::clicked, this, &SettingsDialog::testConnection);
    auto *socketRow = new QHBoxLayout;
    socketRow->addWidget(m_socketPath, 1);
    socketRow->addWidget(testButton);

    m_testIcon = new QLabel;
    m_testIcon->setVisible(false);
    m_testResult = new QLabel;
    m_testResult->setTextFormat(Qt::PlainText);
    m_testResult->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_testResult->setVisible(false);
    auto *testRow = new QHBoxLayout;
    testRow->addWidget(m_testIcon, 0, Qt::AlignTop);
    testRow->addWidget(m_testResult, 1);
    connect(m_testClient, &ClamdClient::versionReceived, this, [this](const ClamdVersion &version) {
        Widgets::setIcon(m_testIcon, StatusDisplay::icon(ClamdWatcher::State::Connected),
                         style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this));
        m_testResult->setText(version.signatures.isEmpty()
                                  ? tr("clamd répond : ClamAV %1.").arg(version.engine)
                                  : tr("clamd répond : ClamAV %1, signatures n° %2.").arg(version.engine, version.signatures));
    });
    connect(m_testClient, &ClamdClient::errorOccurred, this, [this](ClamdClient::Error, const QString &message) {
        Widgets::setIcon(m_testIcon, StatusDisplay::icon(ClamdWatcher::State::Error),
                         style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this));
        m_testResult->setText(message);
    });

    auto *connectionForm = new QFormLayout;
    connectionForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    connectionForm->addRow(tr("Socket :"), socketRow);
    connectionForm->addRow(QString(), testRow);
    auto *connection = new QVBoxLayout;
    connection->addLayout(connectionForm);
    connection->addWidget(note(tr("Laissez vide pour la détection automatique : directive LocalSocket de la "
                                  "configuration de clamd, puis emplacements usuels (Fedora, Debian, Ubuntu). "
                                  "L'option --socket de la ligne de commande a priorité.")));

    m_checkInterval = new QSpinBox;
    m_checkInterval->setRange(5, 3600);
    m_checkInterval->setSuffix(tr(" s"));
    m_signaturesMaxAge = new QSpinBox;
    m_signaturesMaxAge->setRange(0, 365);
    m_signaturesMaxAge->setSpecialValueText(tr("Jamais"));
    m_signaturesMaxAge->setSuffix(tr(" jours"));
    auto *monitoringForm = new QFormLayout;
    monitoringForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    monitoringForm->addRow(tr("Vérifier l'état de clamd toutes les :"), m_checkInterval);
    monitoringForm->addRow(tr("Signatures obsolètes après :"), m_signaturesMaxAge);
    auto *monitoring = new QVBoxLayout;
    monitoring->addLayout(monitoringForm);
    monitoring->addWidget(note(tr("freshclam met normalement les signatures à jour plusieurs fois par jour : "
                                  "des signatures anciennes indiquent qu'il ne fonctionne plus.")));

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(group(tr("Connexion"), connection));
    layout->addWidget(group(tr("Surveillance"), monitoring));
    layout->addStretch();
    return page;
}

void SettingsDialog::load()
{
    m_autostart->setChecked(Autostart::isEnabled());
    m_closeToTray->setChecked(Settings::closeToTray());
    m_historyMax->setValue(Settings::historyMaxEntries());

    m_quickScanPaths->setPaths(Settings::quickScanPaths());
    m_excludedPaths->setPaths(Settings::excludedPaths());
    m_scanHidden->setChecked(Settings::scanHidden());
    const int maxFileSize = Settings::maxFileSizeMb();
    m_limitFileSize->setChecked(maxFileSize > 0);
    m_maxFileSize->setValue(maxFileSize > 0 ? maxFileSize : 100);

    m_usbAutoScan->setChecked(Settings::usbAutoScan());
    m_usbNotify->setChecked(Settings::usbNotify());

    m_notifyScanFinished->setChecked(Settings::notifyScanFinished());
    m_notifyRealtime->setChecked(Settings::notifyRealtime());
    m_notifyClamdLost->setChecked(Settings::notifyClamdLost());
    m_notifySignatures->setChecked(Settings::notifySignatures());

    m_socketPath->setText(Settings::socketPath());
    m_checkInterval->setValue(Settings::checkInterval());
    m_signaturesMaxAge->setValue(Settings::signaturesMaxAge());
    updateDependentWidgets();
}

void SettingsDialog::loadDefaults()
{
    // Le démarrage automatique n'est pas un réglage de l'application mais un
    // fichier de la session : il reste tel quel.
    m_closeToTray->setChecked(Settings::Defaults::closeToTray);
    m_historyMax->setValue(Settings::Defaults::historyMaxEntries);

    m_quickScanPaths->setPaths(Settings::Defaults::quickScanPaths());
    m_excludedPaths->setPaths({});
    m_scanHidden->setChecked(Settings::Defaults::scanHidden);
    m_limitFileSize->setChecked(Settings::Defaults::maxFileSizeMb > 0);

    m_usbAutoScan->setChecked(Settings::Defaults::usbAutoScan);
    m_usbNotify->setChecked(Settings::Defaults::usbNotify);

    m_notifyScanFinished->setChecked(Settings::Defaults::notifyScanFinished);
    m_notifyRealtime->setChecked(Settings::Defaults::notifyRealtime);
    m_notifyClamdLost->setChecked(Settings::Defaults::notifyClamdLost);
    m_notifySignatures->setChecked(Settings::Defaults::notifySignatures);

    m_socketPath->clear();
    m_checkInterval->setValue(Settings::Defaults::checkInterval);
    m_signaturesMaxAge->setValue(Settings::Defaults::signaturesMaxAge);
    updateDependentWidgets();
    setModified(true);
}

void SettingsDialog::apply()
{
    Settings::setCloseToTray(m_closeToTray->isChecked());
    Settings::setHistoryMaxEntries(m_historyMax->value());

    // Liste par défaut : rien d'enregistré, pour suivre les dossiers du système.
    const QStringList quick = m_quickScanPaths->paths();
    Settings::setQuickScanPaths(quick == Settings::Defaults::quickScanPaths() ? QStringList() : quick);
    Settings::setExcludedPaths(m_excludedPaths->paths());
    Settings::setScanHidden(m_scanHidden->isChecked());
    Settings::setMaxFileSizeMb(m_limitFileSize->isChecked() ? m_maxFileSize->value() : 0);

    Settings::setUsbAutoScan(m_usbAutoScan->isChecked());
    Settings::setUsbNotify(m_usbNotify->isChecked());

    Settings::setNotifyScanFinished(m_notifyScanFinished->isChecked());
    Settings::setNotifyRealtime(m_notifyRealtime->isChecked());
    Settings::setNotifyClamdLost(m_notifyClamdLost->isChecked());
    Settings::setNotifySignatures(m_notifySignatures->isChecked());

    Settings::setSocketPath(m_socketPath->text().trimmed());
    Settings::setCheckInterval(m_checkInterval->value());
    Settings::setSignaturesMaxAge(m_signaturesMaxAge->value());

    QString error;
    if (m_autostart->isChecked() != Autostart::isEnabled() && !Autostart::setEnabled(m_autostart->isChecked(), &error))
        QMessageBox::warning(this, tr("Démarrage automatique"), error);

    setModified(false);
    emit applied();
}

void SettingsDialog::accept()
{
    if (m_modified)
        apply();
    QDialog::accept();
}

void SettingsDialog::setModified(bool modified)
{
    m_modified = modified;
    m_buttons->button(QDialogButtonBox::Apply)->setEnabled(modified);
}

void SettingsDialog::watchChanges()
{
    const auto modified = [this] {
        updateDependentWidgets();
        setModified(true);
    };
    for (QCheckBox *box : {m_autostart, m_closeToTray, m_scanHidden, m_limitFileSize, m_usbAutoScan, m_usbNotify,
                           m_notifyScanFinished, m_notifyRealtime, m_notifyClamdLost, m_notifySignatures})
        connect(box, &QCheckBox::toggled, this, modified);
    for (QSpinBox *spin : {m_historyMax, m_maxFileSize, m_checkInterval, m_signaturesMaxAge})
        connect(spin, &QSpinBox::valueChanged, this, modified);
    for (PathListEdit *list : {m_quickScanPaths, m_excludedPaths})
        connect(list, &PathListEdit::changed, this, modified);
    connect(m_socketPath, &QLineEdit::textChanged, this, modified);
}

void SettingsDialog::updateDependentWidgets()
{
    m_usbNotify->setEnabled(m_usbAutoScan->isChecked());
    m_maxFileSize->setEnabled(m_limitFileSize->isChecked());
}

void SettingsDialog::testConnection()
{
    const QString path = m_socketPath->text().trimmed();
    m_testClient->setSocketPath(path.isEmpty() ? ClamdClient::detectSocketPath() : path);
    m_testIcon->setVisible(true);
    Widgets::setIcon(m_testIcon, StatusDisplay::icon(ClamdWatcher::State::Unknown),
                     style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this));
    m_testResult->setText(tr("Test de %1…").arg(m_testClient->socketPath()));
    m_testResult->setVisible(true);
    m_testClient->version();
}
