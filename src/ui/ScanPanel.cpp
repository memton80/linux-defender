#include "ScanPanel.h"

#include "FileActions.h"
#include "ScanResultsModel.h"
#include "StatusDisplay.h"
#include "Widgets.h"
#include "core/ScanHistory.h"
#include "core/Settings.h"

#include <QButtonGroup>
#include <QDate>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QStyle>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <climits>
#include <iterator>

namespace
{
// Tuile de statistique : grand nombre, légende en dessous.
Card *statTile(const QString &caption, QLabel **value)
{
    auto *card = new Card;
    *value = new QLabel(QStringLiteral("—"));
    (*value)->setFont(Widgets::scaledFont((*value)->font(), 1.6, true));
    auto *label = new QLabel(caption);
    Widgets::setSecondary(label);
    auto *layout = new QVBoxLayout(card);
    layout->setSpacing(0);
    layout->addWidget(*value);
    layout->addWidget(label);
    return card;
}

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

// Champ CSV entre guillemets (RFC 4180) : chemins avec virgules ou guillemets compris.
QByteArray csvField(QString text)
{
    text.replace(QLatin1Char('"'), QLatin1String("\"\""));
    return '"' + text.toUtf8() + '"';
}

// Filtres de la liste, dans l'ordre des boutons (liste vide : tous les statuts).
enum Filter { AllFilter, ThreatsFilter, WarningsFilter, ErrorsFilter, CleanFilter, FilterCount };
const QList<ScanResult::Status> kFilterStatuses[FilterCount] = {
    {},
    {ScanResult::Status::Infected},
    {ScanResult::Status::Suspicious, ScanResult::Status::Unscanned},
    {ScanResult::Status::Error},
    {ScanResult::Status::Clean},
};
}

ScanPanel::ScanPanel(ScanManager *scans, ScanHistory *history, QWidget *parent)
    : QWidget(parent)
    , m_scans(scans)
    , m_history(history)
    , m_model(new ScanResultsModel(this))
    , m_filter(new ScanResultsFilter(this))
{
    // Lancement des analyses.
    m_quickButton = new QPushButton(QIcon::fromTheme(QStringLiteral("system-search")), tr("Analyse rapide"));
    m_fullButton = new QPushButton(QIcon::fromTheme(QStringLiteral("user-home")), tr("Analyse complète"));
    m_fullButton->setToolTip(tr("Analyse de tout votre dossier personnel (%1)").arg(QDir::homePath()));
    m_folderButton = new QPushButton(QIcon::fromTheme(QStringLiteral("folder-open")), tr("Dossier…"));
    m_folderButton->setToolTip(tr("Analyser un dossier de votre choix"));
    m_fileButton = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open")), tr("Fichiers…"));
    m_fileButton->setToolTip(tr("Analyser un ou plusieurs fichiers"));
    m_stopButton = new QPushButton(QIcon::fromTheme(QStringLiteral("process-stop")), tr("Arrêter"));
    connect(m_quickButton, &QPushButton::clicked, this, &ScanPanel::startQuickScan);
    connect(m_fullButton, &QPushButton::clicked, this, &ScanPanel::startFullScan);
    connect(m_folderButton, &QPushButton::clicked, this, &ScanPanel::chooseFolder);
    connect(m_fileButton, &QPushButton::clicked, this, &ScanPanel::chooseFiles);
    connect(m_stopButton, &QPushButton::clicked, m_scans, &ScanManager::cancelAll);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_quickButton);
    buttons->addWidget(m_fullButton);
    buttons->addWidget(m_folderButton);
    buttons->addWidget(m_fileButton);
    buttons->addStretch();
    buttons->addWidget(m_stopButton);

    // Activité : scan en cours, ou bilan du dernier scan.
    m_activityIcon = new QLabel;
    m_activityTitle = new QLabel;
    m_activityTitle->setTextFormat(Qt::PlainText);
    m_activityTitle->setFont(Widgets::scaledFont(m_activityTitle->font(), 1.15, true));
    // Taille horizontale ignorée : un chemin très long est coupé au lieu
    // d'élargir la fenêtre (il reste lisible dans l'infobulle).
    m_activityTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_activityDetail = new QLabel;
    m_activityDetail->setTextFormat(Qt::PlainText);
    m_activityDetail->setWordWrap(true);
    m_activityDetail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *activityTexts = new QVBoxLayout;
    activityTexts->addWidget(m_activityTitle);
    activityTexts->addWidget(m_activityDetail);
    auto *activityHeader = new QHBoxLayout;
    activityHeader->addWidget(m_activityIcon, 0, Qt::AlignTop);
    activityHeader->addLayout(activityTexts, 1);

    m_progress = new QProgressBar;
    m_progress->setVisible(false);
    m_currentFile = new QLabel;
    m_currentFile->setTextFormat(Qt::PlainText);
    m_currentFile->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    Widgets::setSecondary(m_currentFile);
    m_currentFile->setVisible(false);

    m_activity = new Card;
    auto *activityLayout = new QVBoxLayout(m_activity);
    activityLayout->addLayout(activityHeader);
    activityLayout->addWidget(m_progress);
    activityLayout->addWidget(m_currentFile);

    auto *stats = new QHBoxLayout;
    stats->addWidget(statTile(tr("Fichiers analysés"), &m_scannedValue));
    stats->addWidget(statTile(tr("Menaces"), &m_threatsValue));
    Card *warningsTile = statTile(tr("Avertissements"), &m_warningsValue);
    warningsTile->setToolTip(tr("Fichiers suspects (détection heuristique, programme potentiellement indésirable) "
                                "et fichiers que clamd n'a pas pu analyser (archive chiffrée, fichier trop gros)"));
    stats->addWidget(warningsTile);
    stats->addWidget(statTile(tr("Erreurs"), &m_errorsValue));
    stats->addWidget(statTile(tr("Durée"), &m_durationValue));

    // Filtres de la liste : statut, recherche.
    m_filterGroup = new QButtonGroup(this);
    const QIcon filterIcons[FilterCount] = {
        QIcon(),
        StatusDisplay::threatIcon(),
        StatusDisplay::resultIcon(ScanResult::Status::Suspicious),
        StatusDisplay::resultIcon(ScanResult::Status::Error),
        StatusDisplay::resultIcon(ScanResult::Status::Clean),
    };
    auto *filters = new QHBoxLayout;
    for (int i = 0; i < FilterCount; ++i) {
        auto *button = new QToolButton;
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setIcon(filterIcons[i]);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_filterGroup->addButton(button, i);
        m_filterButtons << button;
        filters->addWidget(button);
    }
    m_filterButtons.first()->setChecked(true);
    connect(m_filterGroup, &QButtonGroup::idClicked, this, [this](int id) {
        m_filter->setStatuses(kFilterStatuses[id]);
        updatePlaceholder();
    });

    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Rechercher…"));
    m_search->setToolTip(tr("Filtrer la liste par chemin ou par nom de menace"));
    m_search->setClearButtonEnabled(true);
    m_search->addAction(QIcon::fromTheme(QStringLiteral("edit-find")), QLineEdit::LeadingPosition);
    m_search->setMaximumWidth(30 * fontMetrics().averageCharWidth());
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_filter->setText(text);
        updatePlaceholder();
    });

    m_exportButton = new QPushButton(QIcon::fromTheme(QStringLiteral("document-export")), tr("Exporter…"));
    m_exportButton->setToolTip(tr("Enregistrer la liste affichée dans un fichier CSV"));
    connect(m_exportButton, &QPushButton::clicked, this, &ScanPanel::exportResults);

    filters->addStretch();
    filters->addWidget(m_search, 1);
    filters->addWidget(m_exportButton);

    // Liste des résultats : menaces en tête, puis erreurs, puis fichiers sains.
    m_filter->setSourceModel(m_model);
    m_view = new QTreeView;
    m_view->setModel(m_filter);
    m_view->setRootIsDecorated(false);
    m_view->setUniformRowHeights(true); // indispensable pour rester fluide avec beaucoup de lignes
    m_view->setAlternatingRowColors(true);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    // Texte trop long coupé au milieu : on garde le début du chemin et le nom du fichier.
    m_view->setTextElideMode(Qt::ElideMiddle);
    m_view->setSortingEnabled(true);
    m_view->sortByColumn(ScanResultsModel::StatusColumn, Qt::AscendingOrder);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QTreeView::customContextMenuRequested, this, &ScanPanel::showContextMenu);
    connect(m_view, &QTreeView::doubleClicked, this, [](const QModelIndex &index) {
        FileActions::showInFileManager(index.siblingAtColumn(ScanResultsModel::PathColumn).data().toString());
    });
    QHeaderView *header = m_view->header();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(ScanResultsModel::PathColumn, QHeaderView::Stretch);
    // Largeurs initiales tirées de la police du système, pas de valeurs en pixels.
    const int charWidth = fontMetrics().averageCharWidth();
    const int iconWidth = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
    int statusWidth = 0;
    for (int status = 0; status < ScanResult::kStatusCount; ++status)
        statusWidth = qMax(statusWidth, fontMetrics().horizontalAdvance(StatusDisplay::resultText(ScanResult::Status(status))));
    // Le texte en gras (menaces) est un peu plus large : marge de 4 caractères.
    header->resizeSection(ScanResultsModel::StatusColumn, iconWidth + statusWidth + 4 * charWidth);
    header->resizeSection(ScanResultsModel::DetailColumn, 40 * charWidth);
    m_viewStack = new PlaceholderStack(m_view, QString());

    m_limitNote = new QLabel(tr("Seuls les %1 premiers fichiers sains sont listés ; les compteurs incluent tous les fichiers.")
                                 .arg(QLocale().toString(ScanResultsModel::kMaxCleanRows)));
    Widgets::setSecondary(m_limitNote);
    m_limitNote->setVisible(false);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(Widgets::pageTitle(tr("Analyse")));
    layout->addLayout(buttons);
    layout->addWidget(m_activity);
    layout->addLayout(stats);
    layout->addSpacing(style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing));
    layout->addLayout(filters);
    layout->addWidget(m_viewStack, 1);
    layout->addWidget(m_limitNote);

    m_clock.setInterval(1000);
    connect(&m_clock, &QTimer::timeout, this, &ScanPanel::updateElapsed);

    connect(m_scans, &ScanManager::scanStarted, this, &ScanPanel::onScanStarted);
    connect(m_scans, &ScanManager::counting, this, &ScanPanel::onCounting);
    connect(m_scans, &ScanManager::progressChanged, this, &ScanPanel::onProgress);
    connect(m_scans, &ScanManager::resultsReady, this, &ScanPanel::onResults);
    connect(m_scans, &ScanManager::scanFinished, this, &ScanPanel::onScanFinished);

    // Au lancement : bilan de la dernière analyse enregistrée.
    if (const std::optional<ScanRecord> last = m_history->last())
        showSummary(last->toSummary(), last->origin);
    else
        showIdle();
    updateFilterButtons();
    updateButtons();
    updatePlaceholder();
}

void ScanPanel::chooseFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Fichiers à analyser"), QDir::homePath());
    if (!files.isEmpty())
        m_scans->scan(files, ScanManager::Origin::Manual);
}

void ScanPanel::chooseFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Dossier à analyser"), QDir::homePath());
    if (!folder.isEmpty())
        m_scans->scan({folder}, ScanManager::Origin::Manual);
}

void ScanPanel::startQuickScan()
{
    m_scans->scan(Settings::quickScanPaths(), ScanManager::Origin::Quick);
}

void ScanPanel::startFullScan()
{
    m_scans->scan({QDir::homePath()}, ScanManager::Origin::Full);
}

void ScanPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Textes relatifs (« il y a 5 minutes ») et dossiers de l'analyse rapide à jour.
    if (m_hasSummary && !m_scans->isScanning())
        showSummary(m_summary, m_origin);
    updateButtons();
}

void ScanPanel::onScanStarted(const QStringList &paths, ScanManager::Origin origin)
{
    m_model->clear();
    m_hasSummary = false;
    m_origin = origin;
    m_target = StatusDisplay::targetText(origin, paths);
    m_done = 0;
    std::fill(std::begin(m_counts), std::end(m_counts), 0);

    const QString title = tr("%1 : %2").arg(StatusDisplay::originText(origin), m_target);
    m_activity->setLevel(StatusDisplay::Level::Neutral);
    Widgets::setIcon(m_activityIcon, StatusDisplay::scanningIcon(),
                     style()->pixelMetric(QStyle::PM_LargeIconSize, nullptr, this));
    m_activityTitle->setText(title);
    m_activityTitle->setToolTip(paths.join(QLatin1Char('\n')));
    m_progress->setRange(0, 0); // animation « occupé » pendant le comptage
    m_progress->setVisible(true);
    m_currentFile->clear();
    m_currentFile->setVisible(true);
    m_limitNote->setVisible(false);

    m_elapsed.start();
    m_clock.start();
    setStats(number(0), number(0), number(0), number(0), StatusDisplay::durationText(0));
    onCounting(0);
    updateFilterButtons();
    updateButtons();
    updatePlaceholder();
}

void ScanPanel::onCounting(qint64 found)
{
    m_activityDetail->setText(found > 1 ? tr("Recherche des fichiers à analyser… (%1 trouvés)").arg(number(found))
                                        : tr("Recherche des fichiers à analyser…"));
}

void ScanPanel::onProgress(qint64 done, qint64 total)
{
    m_done = done;
    m_progress->setRange(0, int(qMin<qint64>(total, INT_MAX)));
    m_progress->setValue(int(qMin<qint64>(done, INT_MAX)));
    m_activityDetail->setText(tr("%1 fichiers analysés sur %2").arg(number(done), number(total)));
    m_scannedValue->setText(number(done));
}

void ScanPanel::onResults(const QList<ScanResult> &results)
{
    if (results.isEmpty())
        return;
    m_model->append(results);
    for (const ScanResult &result : results)
        ++m_counts[int(result.status)];

    // Dernier fichier reçu : celui que clamd vient d'analyser.
    const QString path = results.last().path;
    m_currentFile->setText(m_currentFile->fontMetrics().elidedText(path, Qt::ElideMiddle, m_currentFile->width()));
    m_currentFile->setToolTip(path);
    m_threatsValue->setText(number(m_counts[int(ScanResult::Status::Infected)]));
    m_warningsValue->setText(number(m_counts[int(ScanResult::Status::Suspicious)]
                                    + m_counts[int(ScanResult::Status::Unscanned)]));
    m_errorsValue->setText(number(m_counts[int(ScanResult::Status::Error)]));
    updateFilterButtons();
    updateButtons();
}

void ScanPanel::onScanFinished(const ScanSummary &summary, ScanManager::Origin origin)
{
    m_clock.stop();
    showSummary(summary, origin);
    updateButtons();
    updatePlaceholder();
}

void ScanPanel::showSummary(const ScanSummary &summary, ScanManager::Origin origin)
{
    m_hasSummary = true;
    m_summary = summary;
    m_origin = origin;
    m_target = StatusDisplay::targetText(origin, summary.paths);

    const StatusDisplay::Level level = StatusDisplay::summaryLevel(summary);
    const QIcon icon = summary.infected > 0 ? StatusDisplay::threatIcon() : StatusDisplay::levelIcon(level);
    m_activity->setLevel(level);
    Widgets::setIcon(m_activityIcon, icon, style()->pixelMetric(QStyle::PM_LargeIconSize, nullptr, this));
    const QString title = tr("Dernière analyse — %1 : %2").arg(StatusDisplay::originText(origin), m_target);
    m_activityTitle->setText(title);
    m_activityTitle->setToolTip(summary.paths.join(QLatin1Char('\n')));

    QString detail = StatusDisplay::summaryText(summary);
    if (summary.started.isValid())
        detail += QLatin1Char('\n')
            + tr("%1 · durée : %2")
                  .arg(capitalized(StatusDisplay::relativeTime(summary.started.addMSecs(summary.elapsedMsecs))),
                       StatusDisplay::durationText(summary.elapsedMsecs));
    m_activityDetail->setText(detail);
    m_progress->setVisible(false);
    m_currentFile->setVisible(false);
    m_limitNote->setVisible(m_model->unlistedCleanCount() > 0);

    setStats(number(summary.scanned), number(summary.infected), number(summary.suspicious + summary.unscanned),
             number(summary.errors), StatusDisplay::durationText(summary.elapsedMsecs));
}

void ScanPanel::showIdle()
{
    m_activity->setLevel(StatusDisplay::Level::Neutral);
    Widgets::setIcon(m_activityIcon, StatusDisplay::levelIcon(StatusDisplay::Level::Neutral),
                     style()->pixelMetric(QStyle::PM_LargeIconSize, nullptr, this));
    m_activityTitle->setText(tr("Aucune analyse pour l'instant"));
    m_activityDetail->setText(tr("L'analyse rapide vérifie en quelques instants les dossiers où arrivent les "
                                 "nouveaux fichiers : %1.")
                                  .arg(StatusDisplay::targetText(ScanManager::Origin::Quick, Settings::quickScanPaths())));
    const QString none = QStringLiteral("—");
    setStats(none, none, none, none, none);
}

void ScanPanel::setStats(const QString &scanned, const QString &threats, const QString &warnings,
                         const QString &errors, const QString &duration)
{
    m_scannedValue->setText(scanned);
    m_threatsValue->setText(threats);
    m_warningsValue->setText(warnings);
    m_errorsValue->setText(errors);
    m_durationValue->setText(duration);
}

void ScanPanel::updateElapsed()
{
    m_durationValue->setText(StatusDisplay::durationText(m_elapsed.elapsed()));
}

void ScanPanel::updateFilterButtons()
{
    const qint64 clean = m_counts[int(ScanResult::Status::Clean)];
    const qint64 infected = m_counts[int(ScanResult::Status::Infected)];
    const qint64 warnings = m_counts[int(ScanResult::Status::Suspicious)] + m_counts[int(ScanResult::Status::Unscanned)];
    const qint64 errors = m_counts[int(ScanResult::Status::Error)];
    m_filterButtons.at(AllFilter)->setText(tr("Tous (%1)").arg(number(clean + infected + warnings + errors)));
    m_filterButtons.at(ThreatsFilter)->setText(tr("Menaces (%1)").arg(number(infected)));
    m_filterButtons.at(WarningsFilter)->setText(tr("Avertissements (%1)").arg(number(warnings)));
    m_filterButtons.at(WarningsFilter)->setToolTip(tr("Fichiers suspects, et fichiers que clamd n'a pas pu analyser"));
    m_filterButtons.at(ErrorsFilter)->setText(tr("Erreurs (%1)").arg(number(errors)));
    m_filterButtons.at(CleanFilter)->setText(tr("Sains (%1)").arg(number(clean)));
}

void ScanPanel::updatePlaceholder()
{
    QString text;
    if (m_model->rowCount() == 0) {
        text = m_scans->isScanning() ? tr("Analyse en cours…")
                                     : tr("Les fichiers de la prochaine analyse s'afficheront ici.");
    } else if (!m_search->text().trimmed().isEmpty()) {
        text = tr("Aucun fichier ne correspond à « %1 ».").arg(m_search->text().trimmed());
    } else {
        switch (m_filterGroup->checkedId()) {
        case ThreatsFilter:
            text = tr("Aucune menace détectée.");
            break;
        case WarningsFilter:
            text = tr("Aucun fichier suspect ou non analysé.");
            break;
        case ErrorsFilter:
            text = tr("Aucune erreur.");
            break;
        default:
            text = tr("Aucun fichier sain.");
            break;
        }
    }
    m_viewStack->setPlaceholderText(text);
}

void ScanPanel::updateButtons()
{
    const bool scanning = m_scans->isScanning();
    m_quickButton->setEnabled(!scanning);
    m_fullButton->setEnabled(!scanning);
    m_folderButton->setEnabled(!scanning);
    m_fileButton->setEnabled(!scanning);
    m_stopButton->setEnabled(scanning);
    m_exportButton->setEnabled(!scanning && m_model->rowCount() > 0);
    m_quickButton->setToolTip(tr("Analyse de : %1").arg(Settings::quickScanPaths().join(QStringLiteral(", "))));
}

void ScanPanel::showContextMenu(const QPoint &position)
{
    const QModelIndex index = m_view->indexAt(position);
    if (!index.isValid())
        return;
    const auto status = ScanResult::Status(index.data(ScanResultsModel::StatusRole).toInt());
    const bool threat = status == ScanResult::Status::Infected || status == ScanResult::Status::Suspicious;
    FileActions::execContextMenu(this, m_view->viewport()->mapToGlobal(position),
                                 index.siblingAtColumn(ScanResultsModel::PathColumn).data().toString(),
                                 index.siblingAtColumn(ScanResultsModel::DetailColumn).data().toString(),
                                 threat ? tr("Copier le nom de la menace") : tr("Copier le détail"));
}

void ScanPanel::exportResults()
{
    const QString suggested =
        QDir::home().filePath(tr("analyse-%1.csv").arg(QDate::currentDate().toString(Qt::ISODate)));
    const QString fileName =
        QFileDialog::getSaveFileName(this, tr("Exporter les résultats"), suggested, tr("Fichiers CSV (*.csv)"));
    if (fileName.isEmpty())
        return;

    // La liste telle qu'elle est affichée : filtre et tri compris.
    QByteArray csv = csvField(tr("Statut")) + ',' + csvField(tr("Fichier")) + ',' + csvField(tr("Détail")) + '\n';
    for (int row = 0; row < m_filter->rowCount(); ++row) {
        csv += csvField(m_filter->index(row, ScanResultsModel::StatusColumn).data().toString()) + ','
            + csvField(m_filter->index(row, ScanResultsModel::PathColumn).data().toString()) + ','
            + csvField(m_filter->index(row, ScanResultsModel::DetailColumn).data().toString()) + '\n';
    }

    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly) || file.write(csv) != csv.size() || !file.commit())
        QMessageBox::warning(this, tr("Exporter les résultats"),
                             tr("Impossible d'enregistrer %1 :\n%2").arg(fileName, file.errorString()));
}
