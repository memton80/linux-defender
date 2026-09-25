#include "HistoryPanel.h"

#include "FileActions.h"
#include "HistoryModel.h"
#include "StatusDisplay.h"
#include "Widgets.h"
#include "core/ScanHistory.h"
#include "core/Settings.h"
#include "core/ThreatText.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStyle>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>

HistoryPanel::HistoryPanel(ScanHistory *history, QWidget *parent)
    : QWidget(parent)
    , m_history(history)
    , m_model(new HistoryModel(history, this))
{
    m_info = new QLabel;
    Widgets::setSecondary(m_info);
    m_clearButton = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-clear-history")), tr("Effacer l'historique"));
    connect(m_clearButton, &QPushButton::clicked, this, &HistoryPanel::clearHistory);

    auto *header = new QHBoxLayout;
    header->addWidget(Widgets::pageTitle(tr("Historique")));
    header->addStretch();
    header->addWidget(m_clearButton);

    // Liste des analyses.
    m_view = new QTreeView;
    m_view->setModel(m_model);
    m_view->setRootIsDecorated(false);
    m_view->setUniformRowHeights(true);
    m_view->setAlternatingRowColors(true);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setTextElideMode(Qt::ElideMiddle);
    QHeaderView *columns = m_view->header();
    columns->setStretchLastSection(false);
    columns->setSectionResizeMode(HistoryModel::TargetColumn, QHeaderView::Stretch);
    const QFontMetrics metrics = fontMetrics();
    const int charWidth = metrics.averageCharWidth();
    const int iconWidth = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
    columns->resizeSection(HistoryModel::DateColumn,
                           iconWidth + metrics.horizontalAdvance(QLocale().toString(QDateTime::currentDateTime(),
                                                                                   QLocale::ShortFormat))
                               + 4 * charWidth);
    int typeWidth = 0;
    for (const auto origin : {ScanManager::Origin::Manual, ScanManager::Origin::Usb, ScanManager::Origin::Quick,
                              ScanManager::Origin::Full, ScanManager::Origin::Scheduled})
        typeWidth = qMax(typeWidth, metrics.horizontalAdvance(StatusDisplay::originText(origin)));
    columns->resizeSection(HistoryModel::TypeColumn, typeWidth + 3 * charWidth);
    for (const int column : {HistoryModel::ScannedColumn, HistoryModel::ThreatsColumn, HistoryModel::WarningsColumn,
                             HistoryModel::ErrorsColumn, HistoryModel::DurationColumn})
        columns->resizeSection(column, metrics.horizontalAdvance(m_model->headerData(column, Qt::Horizontal).toString())
                                           + 4 * charWidth);
    connect(m_view->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &HistoryPanel::showDetails);
    m_viewStack = new PlaceholderStack(m_view, tr("Aucune analyse enregistrée pour l'instant."));

    // Détails de l'analyse choisie.
    m_detailsIcon = new QLabel;
    m_detailsTitle = new QLabel;
    m_detailsTitle->setFont(Widgets::scaledFont(m_detailsTitle->font(), 1.15, true));
    m_detailsText = new QLabel;
    m_detailsText->setTextFormat(Qt::PlainText);
    m_detailsText->setWordWrap(true);
    m_detailsText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *texts = new QVBoxLayout;
    texts->addWidget(m_detailsTitle);
    texts->addWidget(m_detailsText);
    auto *detailsHeader = new QHBoxLayout;
    detailsHeader->addWidget(m_detailsIcon, 0, Qt::AlignTop);
    detailsHeader->addLayout(texts, 1);

    m_threatsTitle = new QLabel;
    m_threatsTitle->setFont(Widgets::scaledFont(m_threatsTitle->font(), 1, true));
    m_threats = new QTreeWidget;
    m_threats->setColumnCount(2);
    m_threats->setHeaderLabels({tr("Fichier"), tr("Détail")});
    m_threats->setRootIsDecorated(false);
    m_threats->setUniformRowHeights(true);
    m_threats->setTextElideMode(Qt::ElideMiddle);
    m_threats->setContextMenuPolicy(Qt::CustomContextMenu);
    m_threats->header()->setStretchLastSection(false);
    m_threats->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_threats->header()->resizeSection(1, 36 * charWidth);
    connect(m_threats, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &position) {
        const QTreeWidgetItem *item = m_threats->itemAt(position);
        if (item)
            FileActions::execContextMenu(this, m_threats->viewport()->mapToGlobal(position), item->text(0),
                                         item->text(1), tr("Copier le nom de la menace"));
    });
    connect(m_threats, &QTreeWidget::itemDoubleClicked, this,
            [](const QTreeWidgetItem *item) { FileActions::showInFileManager(item->text(0)); });

    m_details = new Card;
    auto *detailsLayout = new QVBoxLayout(m_details);
    detailsLayout->addLayout(detailsHeader);
    detailsLayout->addWidget(m_threatsTitle);
    detailsLayout->addWidget(m_threats, 1);

    auto *splitter = new QSplitter(Qt::Vertical);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(m_viewStack);
    splitter->addWidget(m_details);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(header);
    layout->addWidget(m_info);
    layout->addWidget(splitter, 1);

    connect(m_history, &ScanHistory::changed, this, [this] {
        selectLatest();
        updateInfo();
    });
    selectLatest();
    updateInfo();
}

void HistoryPanel::selectLatest()
{
    if (m_model->rowCount() > 0)
        m_view->setCurrentIndex(m_model->index(0, 0));
    showDetails();
}

void HistoryPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateInfo(); // nombre d'analyses gardées : peut avoir changé dans les paramètres
}

void HistoryPanel::showDetails()
{
    const QModelIndex current = m_view->currentIndex();
    m_details->setVisible(current.isValid());
    if (!current.isValid())
        return;

    const ScanRecord record = m_model->record(current.row());
    const ScanSummary summary = record.toSummary();
    const StatusDisplay::Level level = StatusDisplay::summaryLevel(summary);
    m_details->setLevel(level);
    Widgets::setIcon(m_detailsIcon, record.infected > 0 ? StatusDisplay::threatIcon() : StatusDisplay::levelIcon(level),
                     style()->pixelMetric(QStyle::PM_LargeIconSize, nullptr, this));
    m_detailsTitle->setText(tr("%1 — %2 à %3")
                                .arg(StatusDisplay::originText(record.origin),
                                     QLocale().toString(record.started.date(), QLocale::LongFormat),
                                     QLocale().toString(record.started.time(), QLocale::ShortFormat)));
    QString target = record.paths.join(QStringLiteral(", "));
    if (record.systemAreas)
        target += tr(", plus démarrage automatique, fichiers temporaires et programmes en cours");
    m_detailsText->setText(tr("%1\nCible : %2\nDurée : %3")
                               .arg(StatusDisplay::summaryText(summary), target,
                                    StatusDisplay::durationText(record.elapsedMsecs)));

    // Menaces, puis fichiers suspects et non analysés.
    m_threats->clear();
    for (const QList<ScanResult> *results : {&record.threats, &record.warnings}) {
        for (const ScanResult &result : *results) {
            auto *item = new QTreeWidgetItem({result.path, result.detail});
            item->setIcon(0, StatusDisplay::resultIcon(result.status));
            item->setToolTip(0, StatusDisplay::resultText(result.status) + QLatin1Char('\n') + result.path);
            item->setToolTip(1, ThreatText::describe(result.detail));
            m_threats->addTopLevelItem(item);
        }
    }
    const bool hasThreats = !record.threats.isEmpty();
    const bool hasWarnings = !record.warnings.isEmpty();
    m_threats->setVisible(hasThreats || hasWarnings);
    m_threatsTitle->setVisible(hasThreats || hasWarnings);
    const bool truncated = record.infected > record.threats.size()
        || record.suspicious + record.unscanned > record.warnings.size();
    QString title = hasThreats && hasWarnings ? tr("Menaces et avertissements")
                    : hasThreats              ? tr("Menaces détectées")
                                              : tr("Fichiers suspects ou non analysés");
    if (truncated)
        title += tr(" (les %1 premiers de chaque catégorie)").arg(ScanSummary::kMaxThreats);
    m_threatsTitle->setText(title + tr(" : les fichiers n'ont été ni supprimés ni déplacés."));
}

void HistoryPanel::updateInfo()
{
    const int max = Settings::historyMaxEntries();
    m_info->setText(max == 0 ? tr("L'historique est désactivé dans les paramètres : les analyses ne sont plus enregistrées.")
                             : tr("Les %1 dernières analyses sont conservées (réglable dans les paramètres).").arg(max));
    m_clearButton->setEnabled(m_model->rowCount() > 0);
}

void HistoryPanel::clearHistory()
{
    if (QMessageBox::question(this, tr("Effacer l'historique"),
                              tr("Effacer toutes les analyses de l'historique ?\nLes fichiers analysés ne sont pas modifiés."))
        == QMessageBox::Yes)
        m_history->clear();
}
