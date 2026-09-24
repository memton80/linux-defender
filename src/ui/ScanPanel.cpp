#include "ScanPanel.h"

#include "ScanResultsModel.h"
#include "StatusDisplay.h"

#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStyle>
#include <QTreeView>
#include <QVBoxLayout>

#include <climits>

ScanPanel::ScanPanel(ScanManager *scans, QWidget *parent)
    : QWidget(parent)
    , m_scans(scans)
    , m_model(new ScanResultsModel(this))
    , m_proxy(new QSortFilterProxyModel(this))
{
    m_fileButton = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open")), tr("Scanner des fichiers…"));
    m_folderButton = new QPushButton(QIcon::fromTheme(QStringLiteral("folder-open")), tr("Scanner un dossier…"));
    m_stopButton = new QPushButton(QIcon::fromTheme(QStringLiteral("process-stop")), tr("Arrêter"));
    connect(m_fileButton, &QPushButton::clicked, this, &ScanPanel::chooseFiles);
    connect(m_folderButton, &QPushButton::clicked, this, &ScanPanel::chooseFolder);
    connect(m_stopButton, &QPushButton::clicked, m_scans, &ScanManager::cancelAll);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_fileButton);
    buttons->addWidget(m_folderButton);
    buttons->addStretch();
    buttons->addWidget(m_stopButton);

    // Activité en cours. Taille horizontale ignorée : un chemin très long est
    // coupé au lieu d'élargir la fenêtre (il reste lisible dans l'infobulle).
    m_activity = new QLabel(tr("Aucun scan en cours."));
    m_activity->setTextFormat(Qt::PlainText);
    m_activity->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_progress = new QProgressBar;
    m_progress->setVisible(false);

    // Résumé du dernier scan.
    m_summaryIcon = new QLabel;
    m_summaryIcon->setVisible(false);
    m_summary = new QLabel;
    m_summary->setTextFormat(Qt::PlainText);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *summaryRow = new QHBoxLayout;
    summaryRow->addWidget(m_summaryIcon, 0, Qt::AlignTop);
    summaryRow->addWidget(m_summary, 1);

    // Liste des résultats : menaces en tête, puis erreurs, puis fichiers sains.
    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(ScanResultsModel::SortRole);
    m_proxy->setFilterRole(ScanResultsModel::StatusRole);
    m_proxy->setDynamicSortFilter(true);

    m_hideClean = new QCheckBox(tr("Masquer les fichiers sains"));
    connect(m_hideClean, &QCheckBox::toggled, this, [this](bool hide) {
        // Le filtre porte sur StatusRole : 1 = infecté, 2 = erreur.
        m_proxy->setFilterRegularExpression(hide ? QStringLiteral("^[12]$") : QString());
    });

    m_view = new QTreeView;
    m_view->setModel(m_proxy);
    m_view->setRootIsDecorated(false);
    m_view->setUniformRowHeights(true); // indispensable pour rester fluide avec beaucoup de lignes
    m_view->setAlternatingRowColors(true);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    // Texte trop long coupé au milieu : on garde le début du chemin et le nom du fichier.
    m_view->setTextElideMode(Qt::ElideMiddle);
    m_view->setSortingEnabled(true);
    m_view->sortByColumn(ScanResultsModel::StatusColumn, Qt::AscendingOrder);
    QHeaderView *header = m_view->header();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(ScanResultsModel::PathColumn, QHeaderView::Stretch);
    // Largeurs initiales tirées de la police du système, pas de valeurs en pixels.
    const int charWidth = fontMetrics().averageCharWidth();
    const int iconWidth = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
    header->resizeSection(ScanResultsModel::StatusColumn, iconWidth + 12 * charWidth);
    header->resizeSection(ScanResultsModel::DetailColumn, 40 * charWidth);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(buttons);
    layout->addWidget(m_activity);
    layout->addWidget(m_progress);
    layout->addLayout(summaryRow);
    layout->addWidget(m_hideClean);
    layout->addWidget(m_view, 1);

    connect(m_scans, &ScanManager::scanStarted, this, &ScanPanel::onScanStarted);
    connect(m_scans, &ScanManager::counting, this, &ScanPanel::onCounting);
    connect(m_scans, &ScanManager::progressChanged, this, &ScanPanel::onProgress);
    connect(m_scans, &ScanManager::resultsReady, this, &ScanPanel::onResults);
    connect(m_scans, &ScanManager::scanFinished, this, &ScanPanel::onScanFinished);
    updateButtons();
}

void ScanPanel::chooseFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Fichiers à scanner"), QDir::homePath());
    if (!files.isEmpty())
        m_scans->scan(files, ScanManager::Origin::Manual);
}

void ScanPanel::chooseFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Dossier à scanner"), QDir::homePath());
    if (!folder.isEmpty())
        m_scans->scan({folder}, ScanManager::Origin::Manual);
}

void ScanPanel::onScanStarted(const QStringList &paths, ScanManager::Origin origin)
{
    m_model->clear();
    m_currentPaths = StatusDisplay::pathsText(paths);
    if (origin == ScanManager::Origin::Usb)
        m_currentPaths = tr("clé USB %1").arg(m_currentPaths);

    onCounting(0);
    m_progress->setRange(0, 0); // animation « occupé » pendant le comptage
    m_progress->setVisible(true);
    m_summaryIcon->setVisible(false);
    m_summary->clear();
    updateButtons();
}

void ScanPanel::onCounting(qint64 found)
{
    const QString text = tr("Recherche des fichiers : %1 (%2 trouvés)…").arg(m_currentPaths, QLocale().toString(found));
    m_activity->setText(text);
    m_activity->setToolTip(text);
}

void ScanPanel::onProgress(qint64 done, qint64 total)
{
    m_progress->setRange(0, int(qMin<qint64>(total, INT_MAX)));
    m_progress->setValue(int(qMin<qint64>(done, INT_MAX)));
    const QString text = tr("Scan en cours : %1 (%2 / %3 fichiers)")
                             .arg(m_currentPaths, QLocale().toString(done), QLocale().toString(total));
    m_activity->setText(text);
    m_activity->setToolTip(text);
}

void ScanPanel::onResults(const QList<ScanResult> &results)
{
    m_model->append(results);
}

void ScanPanel::onScanFinished(const ScanSummary &summary)
{
    m_progress->setVisible(false);
    m_activity->setText(tr("Aucun scan en cours."));
    m_activity->setToolTip({});

    QIcon icon;
    if (summary.infected > 0)
        icon = StatusDisplay::threatIcon();
    else if (!summary.fatalError.isEmpty() || summary.errors > 0)
        icon = StatusDisplay::resultIcon(ScanResult::Status::Error);
    else
        icon = StatusDisplay::resultIcon(ScanResult::Status::Clean);
    const int size = style()->pixelMetric(QStyle::PM_LargeIconSize, nullptr, this);
    m_summaryIcon->setPixmap(icon.pixmap(QSize(size, size), devicePixelRatioF()));
    m_summaryIcon->setVisible(true);

    QString text = tr("Dernier scan : %1\n%2").arg(m_currentPaths, StatusDisplay::summaryText(summary));
    if (m_model->unlistedCleanCount() > 0)
        text += tr("\nSeuls les %1 premiers fichiers sains sont listés.")
                    .arg(QLocale().toString(ScanResultsModel::kMaxCleanRows));
    m_summary->setText(text);
    updateButtons();
}

void ScanPanel::updateButtons()
{
    const bool scanning = m_scans->isScanning();
    m_fileButton->setEnabled(!scanning);
    m_folderButton->setEnabled(!scanning);
    m_stopButton->setEnabled(scanning);
}
