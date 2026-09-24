#include "DashboardPage.h"

#include "Widgets.h"
#include "core/ClamdWatcher.h"
#include "core/ScanHistory.h"
#include "core/Settings.h"
#include "system/OnAccessController.h"

#include <QCommandLinkButton>
#include <QDir>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

#include <climits>

namespace
{
QString number(qint64 value)
{
    return QLocale().toString(value);
}

QString capitalized(QString text)
{
    if (!text.isEmpty())
        text[0] = text.at(0).toUpper();
    return text;
}

// État court de la protection en temps réel, pour sa tuile (le titre de la
// tuile dit déjà de quoi il s'agit).
QString onAccessShortTitle(OnAccessController::State state)
{
    switch (state) {
    case OnAccessController::State::Active:
        return DashboardPage::tr("Active");
    case OnAccessController::State::Inactive:
        return DashboardPage::tr("Désactivée");
    case OnAccessController::State::Failed:
        return DashboardPage::tr("En erreur");
    case OnAccessController::State::NotInstalled:
        return DashboardPage::tr("clamonacc absent");
    case OnAccessController::State::ServiceMissing:
        return DashboardPage::tr("Service absent");
    case OnAccessController::State::Unknown:
        break;
    }
    return DashboardPage::tr("État inconnu");
}

QLabel *sectionTitle(const QString &text)
{
    auto *label = new QLabel(text);
    label->setFont(Widgets::scaledFont(label->font(), 1.15, true));
    return label;
}
}

DashboardPage::DashboardPage(ClamdWatcher *watcher, ScanManager *scans, OnAccessController *onAccess,
                             ScanHistory *history, QWidget *parent)
    : QWidget(parent)
    , m_watcher(watcher)
    , m_scans(scans)
    , m_onAccess(onAccess)
    , m_history(history)
{
    // Bandeau d'état : grande icône, titre, explication, action la plus utile.
    m_bannerIcon = new QLabel;
    m_bannerTitle = new QLabel;
    m_bannerTitle->setFont(Widgets::scaledFont(m_bannerTitle->font(), 1.6, true));
    // Retour à la ligne automatique : la page défile verticalement, et un
    // long message (erreur de clamd) ne doit pas l'élargir au-delà de la fenêtre.
    m_bannerText = new QLabel;
    m_bannerText->setTextFormat(Qt::PlainText);
    m_bannerText->setWordWrap(true);
    m_bannerText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_bannerButton = new QPushButton;
    connect(m_bannerButton, &QPushButton::clicked, this, [this] {
        switch (m_bannerAction) {
        case BannerAction::Check:
            emit checkRequested();
            break;
        case BannerAction::QuickScan:
            emit quickScanRequested();
            break;
        case BannerAction::ShowOnAccess:
            emit showOnAccessRequested();
            break;
        case BannerAction::ShowHistory:
            emit showHistoryRequested();
            break;
        case BannerAction::None:
            break;
        }
    });
    auto *bannerTexts = new QVBoxLayout;
    bannerTexts->addWidget(m_bannerTitle);
    bannerTexts->addWidget(m_bannerText);
    auto *bannerLayout = new QHBoxLayout;
    const int margin = 2 * style()->pixelMetric(QStyle::PM_LayoutLeftMargin);
    bannerLayout->setContentsMargins(margin, margin, margin, margin);
    bannerLayout->setSpacing(margin);
    bannerLayout->addWidget(m_bannerIcon, 0, Qt::AlignVCenter);
    bannerLayout->addLayout(bannerTexts, 1);
    bannerLayout->addWidget(m_bannerButton, 0, Qt::AlignVCenter);
    m_banner = new Card;
    m_banner->setLayout(bannerLayout);

    // Scan en cours : progression, et accès à la page « Analyse ».
    m_scanTitle = new QLabel;
    m_scanTitle->setTextFormat(Qt::PlainText);
    m_scanTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_scanProgress = new QProgressBar;
    auto *showScan = new QPushButton(tr("Voir l'analyse"));
    connect(showScan, &QPushButton::clicked, this, &DashboardPage::showScanRequested);
    auto *scanIcon = new QLabel;
    Widgets::setIcon(scanIcon, StatusDisplay::scanningIcon(), style()->pixelMetric(QStyle::PM_LargeIconSize, nullptr, this));
    auto *scanTexts = new QVBoxLayout;
    scanTexts->addWidget(m_scanTitle);
    scanTexts->addWidget(m_scanProgress);
    auto *scanLayout = new QHBoxLayout;
    scanLayout->addWidget(scanIcon);
    scanLayout->addLayout(scanTexts, 1);
    scanLayout->addWidget(showScan);
    m_scanCard = new Card;
    m_scanCard->setLayout(scanLayout);
    m_scanCard->setVisible(false);

    // Tuiles d'état.
    m_clamdTile = makeTile(tr("Moteur antivirus"), tr("Vérifier maintenant"), &DashboardPage::checkRequested);
    m_signaturesTile = makeTile(tr("Signatures"), QString(), nullptr);
    m_onAccessTile = makeTile(tr("Protection en temps réel"), tr("Détails"), &DashboardPage::showOnAccessRequested);
    m_lastScanTile = makeTile(tr("Dernière analyse"), tr("Voir l'analyse"), &DashboardPage::showScanRequested);
    m_usbTile = makeTile(tr("Clés USB"), tr("Paramètres"), &DashboardPage::settingsRequested);
    m_historyTile = makeTile(tr("Historique"), tr("Voir l'historique"), &DashboardPage::showHistoryRequested);
    auto *tiles = new QGridLayout;
    tiles->addWidget(m_clamdTile.card, 0, 0);
    tiles->addWidget(m_signaturesTile.card, 0, 1);
    tiles->addWidget(m_onAccessTile.card, 0, 2);
    tiles->addWidget(m_lastScanTile.card, 1, 0);
    tiles->addWidget(m_usbTile.card, 1, 1);
    tiles->addWidget(m_historyTile.card, 1, 2);
    for (int column = 0; column < 3; ++column)
        tiles->setColumnStretch(column, 1);

    // Lancement des analyses.
    m_quickButton = new QCommandLinkButton(tr("Analyse rapide"));
    m_quickButton->setIcon(QIcon::fromTheme(QStringLiteral("system-search")));
    m_fullButton = new QCommandLinkButton(tr("Analyse complète"),
                                          tr("Tout votre dossier personnel (%1)").arg(QDir::homePath()));
    m_fullButton->setIcon(QIcon::fromTheme(QStringLiteral("user-home")));
    m_folderButton = new QCommandLinkButton(tr("Analyser un dossier…"), tr("Un dossier de votre choix, clé USB comprise"));
    m_folderButton->setIcon(QIcon::fromTheme(QStringLiteral("folder-open")));
    m_filesButton = new QCommandLinkButton(tr("Analyser des fichiers…"), tr("Un ou plusieurs fichiers de votre choix"));
    m_filesButton->setIcon(QIcon::fromTheme(QStringLiteral("document-open")));
    connect(m_quickButton, &QCommandLinkButton::clicked, this, &DashboardPage::quickScanRequested);
    connect(m_fullButton, &QCommandLinkButton::clicked, this, &DashboardPage::fullScanRequested);
    connect(m_folderButton, &QCommandLinkButton::clicked, this, &DashboardPage::folderScanRequested);
    connect(m_filesButton, &QCommandLinkButton::clicked, this, &DashboardPage::filesScanRequested);
    auto *actions = new QGridLayout;
    actions->addWidget(m_quickButton, 0, 0);
    actions->addWidget(m_fullButton, 0, 1);
    actions->addWidget(m_folderButton, 1, 0);
    actions->addWidget(m_filesButton, 1, 1);

    auto *content = new QWidget;
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->addWidget(Widgets::pageTitle(tr("Accueil")));
    contentLayout->addWidget(m_banner);
    contentLayout->addWidget(m_scanCard);
    contentLayout->addSpacing(style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing));
    contentLayout->addWidget(sectionTitle(tr("État")));
    contentLayout->addLayout(tiles);
    contentLayout->addSpacing(style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing));
    contentLayout->addWidget(sectionTitle(tr("Analyser")));
    contentLayout->addLayout(actions);
    contentLayout->addStretch();

    // Défilement si la fenêtre est petite, plutôt qu'une taille minimale imposée.
    auto *scroll = new QScrollArea;
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(scroll);

    connect(m_watcher, &ClamdWatcher::statusChanged, this, &DashboardPage::refresh);
    connect(m_watcher, &ClamdWatcher::checkFinished, this, &DashboardPage::updateTiles);
    connect(m_onAccess, &OnAccessController::stateChanged, this, &DashboardPage::refresh);
    connect(m_history, &ScanHistory::changed, this, &DashboardPage::refresh);
    connect(m_scans, &ScanManager::scanStarted, this, &DashboardPage::refresh);
    connect(m_scans, &ScanManager::scanFinished, this, &DashboardPage::refresh);
    connect(m_scans, &ScanManager::counting, this, [this] { m_scanProgress->setRange(0, 0); });
    connect(m_scans, &ScanManager::progressChanged, this, [this](qint64 done, qint64 total) {
        m_scanProgress->setRange(0, int(qMin<qint64>(total, INT_MAX)));
        m_scanProgress->setValue(int(qMin<qint64>(done, INT_MAX)));
    });

    m_clock.setInterval(60 * 1000);
    connect(&m_clock, &QTimer::timeout, this, &DashboardPage::refresh);
    m_clock.start();
    refresh();
}

StatusDisplay::Level DashboardPage::level() const
{
    return m_level;
}

void DashboardPage::setRealtimeThreats(int count)
{
    if (count == m_realtimeThreats)
        return;
    m_realtimeThreats = count;
    refresh();
}

void DashboardPage::refresh()
{
    updateBanner();
    updateTiles();
    updateScanCard();
}

void DashboardPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refresh();
}

DashboardPage::Tile DashboardPage::makeTile(const QString &caption, const QString &buttonText,
                                            void (DashboardPage::*onClick)())
{
    Tile tile;
    tile.icon = new QLabel;
    auto *captionLabel = new QLabel(caption);
    Widgets::setSecondary(captionLabel);
    auto *header = new QHBoxLayout;
    header->addWidget(tile.icon);
    header->addWidget(captionLabel, 1);

    tile.value = new QLabel;
    tile.value->setTextFormat(Qt::PlainText);
    tile.value->setFont(Widgets::scaledFont(tile.value->font(), 1.3, true));
    tile.detail = new QLabel;
    tile.detail->setTextFormat(Qt::PlainText);
    tile.detail->setWordWrap(true);
    tile.detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    tile.detail->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    tile.card = new Card;
    auto *layout = new QVBoxLayout(tile.card);
    layout->addLayout(header);
    layout->addWidget(tile.value);
    layout->addWidget(tile.detail, 1);
    if (!buttonText.isEmpty()) {
        auto *button = new QPushButton(buttonText);
        button->setFlat(true);
        connect(button, &QPushButton::clicked, this, onClick);
        layout->addWidget(button, 0, Qt::AlignLeft);
    }
    return tile;
}

void DashboardPage::setTile(const Tile &tile, const QIcon &icon, const QString &value, const QString &detail)
{
    Widgets::setIcon(tile.icon, icon, style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this));
    tile.value->setText(value);
    tile.detail->setText(detail);
}

void DashboardPage::updateBanner()
{
    using Level = StatusDisplay::Level;
    const ClamdWatcher::State clamd = m_watcher->state();
    const OnAccessController::State onAccess = m_onAccess->state();
    const std::optional<ScanRecord> last = m_history->last();

    Level level = Level::Positive;
    QString title;
    QString text;
    BannerAction action = BannerAction::QuickScan;
    QString actionText = tr("Analyse rapide");

    if (clamd == ClamdWatcher::State::Error) {
        level = Level::Negative;
        title = tr("Antivirus indisponible");
        text = tr("clamd ne répond pas : aucune analyse n'est possible.\n%1").arg(m_watcher->errorMessage());
        action = BannerAction::Check;
        actionText = tr("Vérifier maintenant");
    } else if (m_realtimeThreats > 0) {
        level = Level::Negative;
        title = m_realtimeThreats > 1 ? tr("%1 menaces détectées en temps réel").arg(m_realtimeThreats)
                                      : tr("Menace détectée en temps réel");
        text = tr("La protection en temps réel a signalé des fichiers infectés depuis le lancement.\n"
                  "Les fichiers n'ont été ni supprimés ni déplacés : vérifiez-les.");
        action = BannerAction::ShowOnAccess;
        actionText = tr("Voir les détections");
    } else if (last && last->infected > 0) {
        level = Level::Negative;
        title = last->infected > 1 ? tr("%1 menaces détectées").arg(number(last->infected)) : tr("Menace détectée");
        text = tr("Dernière analyse : %1, %2.\n"
                  "Les fichiers n'ont été ni supprimés ni déplacés : vérifiez-les, puis relancez une analyse.")
                   .arg(StatusDisplay::originText(last->origin), StatusDisplay::relativeTime(last->started));
        action = BannerAction::ShowHistory;
        actionText = tr("Voir les menaces");
    } else if (clamd == ClamdWatcher::State::Unknown) {
        level = Level::Neutral;
        title = StatusDisplay::title(clamd);
        action = BannerAction::None;
    } else {
        QStringList warnings;
        const ClamdVersion version = m_watcher->version();
        if (StatusDisplay::signaturesOutdated(version, Settings::signaturesMaxAge()))
            warnings << tr("Les signatures datent de %1 jours : vérifiez que freshclam les met à jour.")
                            .arg(StatusDisplay::signaturesAgeDays(version));
        if (onAccess == OnAccessController::State::Failed)
            warnings << tr("La protection en temps réel est en erreur.");
        if (last && !last->fatalError.isEmpty())
            warnings << tr("La dernière analyse n'a pas pu aller au bout.");

        if (!warnings.isEmpty()) {
            level = Level::Warning;
            title = tr("Attention requise");
            text = warnings.join(QLatin1Char('\n'));
            if (onAccess == OnAccessController::State::Failed) {
                action = BannerAction::ShowOnAccess;
                actionText = tr("Voir le détail");
            }
        } else if (onAccess == OnAccessController::State::Active) {
            title = tr("Votre système est protégé");
            text = tr("clamd répond, les signatures sont à jour et la protection en temps réel surveille vos fichiers.");
        } else {
            title = tr("Antivirus opérationnel");
            text = tr("clamd répond et les signatures sont à jour.\n"
                      "Sans protection en temps réel, les menaces sont détectées lors des analyses.");
        }
    }

    m_bannerAction = action;
    m_banner->setLevel(level);
    const QIcon icon = level == Level::Negative && clamd != ClamdWatcher::State::Error ? StatusDisplay::threatIcon()
                                                                                      : StatusDisplay::levelIcon(level);
    Widgets::setIcon(m_bannerIcon, icon, 2 * style()->pixelMetric(QStyle::PM_LargeIconSize, nullptr, this));
    m_bannerTitle->setText(title);
    m_bannerText->setText(text);
    m_bannerText->setVisible(!text.isEmpty());
    m_bannerButton->setText(actionText);
    m_bannerButton->setVisible(action != BannerAction::None);
    m_bannerButton->setEnabled(action != BannerAction::QuickScan || !m_scans->isScanning());

    if (level != m_level) {
        m_level = level;
        emit levelChanged();
    }
}

void DashboardPage::updateTiles()
{
    const ClamdWatcher::State clamd = m_watcher->state();
    const ClamdVersion version = m_watcher->version();

    // Moteur antivirus.
    QString clamdDetail = clamd == ClamdWatcher::State::Connected ? tr("ClamAV %1").arg(version.engine)
                                                                  : tr("Socket : %1").arg(m_watcher->socketPath());
    if (m_watcher->lastCheck().isValid())
        clamdDetail += QLatin1Char('\n')
            + tr("Vérifié à %1").arg(QLocale().toString(m_watcher->lastCheck().time(), QLocale::ShortFormat));
    setTile(m_clamdTile, StatusDisplay::icon(clamd), StatusDisplay::title(clamd), clamdDetail);

    // Signatures.
    if (clamd == ClamdWatcher::State::Connected && !version.signatures.isEmpty()) {
        const bool outdated = StatusDisplay::signaturesOutdated(version, Settings::signaturesMaxAge());
        QString detail = version.signaturesDate.isValid()
            ? tr("Publiées %1").arg(StatusDisplay::relativeTime(version.signaturesDate))
            : tr("Date inconnue");
        if (outdated)
            detail += QLatin1Char('\n') + tr("Obsolètes : vérifiez le service clamav-freshclam.");
        setTile(m_signaturesTile,
                StatusDisplay::levelIcon(outdated ? StatusDisplay::Level::Warning : StatusDisplay::Level::Positive),
                tr("n° %1").arg(version.signatures), detail);
    } else {
        setTile(m_signaturesTile, StatusDisplay::levelIcon(StatusDisplay::Level::Neutral), tr("Inconnues"),
                clamd == ClamdWatcher::State::Connected ? tr("clamd n'a chargé aucune base de signatures.")
                                                        : tr("Disponibles quand clamd répond."));
    }

    // Protection en temps réel.
    const OnAccessController::State onAccess = m_onAccess->state();
    QString onAccessDetail;
    if (m_realtimeThreats > 0)
        onAccessDetail = m_realtimeThreats > 1 ? tr("%1 détections depuis le lancement").arg(m_realtimeThreats)
                                               : tr("1 détection depuis le lancement");
    else if (onAccess == OnAccessController::State::Active)
        onAccessDetail = m_onAccess->watchedPaths().isEmpty()
            ? tr("Aucune détection depuis le lancement")
            : tr("Surveille %1").arg(m_onAccess->watchedPaths().join(QStringLiteral(", ")));
    else if (onAccess == OnAccessController::State::Inactive)
        onAccessDetail = tr("Service installé mais arrêté : les menaces sont détectées lors des analyses.");
    else if (onAccess == OnAccessController::State::Failed)
        onAccessDetail = tr("Le service a échoué : la cause est dans les détails.");
    else if (onAccess == OnAccessController::State::Unknown)
        onAccessDetail = tr("État du service inconnu.");
    else
        onAccessDetail = tr("Non installée : les détails indiquent comment l'installer.");
    setTile(m_onAccessTile, StatusDisplay::onAccessIcon(onAccess), onAccessShortTitle(onAccess), onAccessDetail);

    // Dernière analyse.
    const std::optional<ScanRecord> last = m_history->last();
    if (last) {
        const ScanSummary summary = last->toSummary();
        const StatusDisplay::Level level = StatusDisplay::summaryLevel(summary);
        QStringList counts{tr("%1 fichiers").arg(number(last->scanned))};
        counts << (last->infected == 0 ? tr("aucune menace")
                                       : last->infected > 1 ? tr("%1 menaces").arg(number(last->infected))
                                                            : tr("1 menace"));
        if (last->errors > 0)
            counts << (last->errors > 1 ? tr("%1 erreurs").arg(number(last->errors)) : tr("1 erreur"));
        setTile(m_lastScanTile, last->infected > 0 ? StatusDisplay::threatIcon() : StatusDisplay::levelIcon(level),
                capitalized(StatusDisplay::relativeTime(last->started)),
                tr("%1\n%2").arg(StatusDisplay::originText(last->origin), counts.join(QStringLiteral(", "))));
    } else {
        setTile(m_lastScanTile, StatusDisplay::levelIcon(StatusDisplay::Level::Neutral), tr("Jamais"),
                tr("Lancez une analyse rapide pour vérifier vos derniers fichiers."));
    }

    // Clés USB.
    const bool usb = Settings::usbAutoScan();
    setTile(m_usbTile,
            StatusDisplay::levelIcon(usb ? StatusDisplay::Level::Positive : StatusDisplay::Level::Neutral),
            usb ? tr("Analyse automatique") : tr("Analyse manuelle"),
            usb ? tr("Les clés USB et cartes mémoire sont analysées dès leur montage.")
                : tr("Les clés USB ne sont pas analysées automatiquement."));

    // Historique.
    const QList<ScanRecord> records = m_history->records();
    qint64 withThreats = 0;
    for (const ScanRecord &record : records)
        withThreats += record.infected > 0 ? 1 : 0;
    const QString historyValue = records.size() > 1 ? tr("%1 analyses").arg(number(records.size()))
                                 : records.size() == 1 ? tr("1 analyse")
                                                       : tr("Aucune analyse");
    QString historyDetail;
    if (Settings::historyMaxEntries() == 0)
        historyDetail = tr("Historique désactivé dans les paramètres.");
    else if (records.isEmpty())
        historyDetail = tr("Les analyses terminées sont enregistrées ici.");
    else if (withThreats == 0)
        historyDetail = tr("Aucune avec des menaces");
    else
        historyDetail = withThreats > 1 ? tr("Dont %1 avec des menaces").arg(number(withThreats))
                                        : tr("Dont 1 avec des menaces");
    setTile(m_historyTile,
            QIcon::fromTheme(QStringLiteral("view-history"), StatusDisplay::levelIcon(StatusDisplay::Level::Neutral)),
            historyValue, historyDetail);
}

void DashboardPage::updateScanCard()
{
    const bool scanning = m_scans->isScanning();
    m_scanCard->setVisible(scanning);
    if (scanning) {
        const ScanManager::Origin origin = m_scans->currentOrigin();
        const QString target = StatusDisplay::targetText(origin, m_scans->currentPaths());
        m_scanTitle->setText(origin == ScanManager::Origin::Usb
                                 ? tr("Analyse de la clé USB en cours : %1").arg(target)
                                 : tr("%1 en cours : %2").arg(StatusDisplay::originText(origin), target));
    }

    m_quickButton->setDescription(StatusDisplay::targetText(ScanManager::Origin::Quick, Settings::quickScanPaths()));
    for (QCommandLinkButton *button : {m_quickButton, m_fullButton, m_folderButton, m_filesButton})
        button->setEnabled(!scanning);
}
