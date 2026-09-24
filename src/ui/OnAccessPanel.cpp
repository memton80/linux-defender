#include "OnAccessPanel.h"

#include "FileActions.h"
#include "OnAccessModel.h"
#include "StatusDisplay.h"
#include "Widgets.h"
#include "system/OnAccessController.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QStyle>
#include <QTreeView>
#include <QVBoxLayout>

OnAccessPanel::OnAccessPanel(OnAccessController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_model(new OnAccessModel(this))
{
    // État du service.
    m_icon = new QLabel;
    m_title = new QLabel;
    m_title->setFont(Widgets::scaledFont(m_title->font(), 1.15, true));
    // Pas de retour à la ligne automatique : les messages contiennent déjà
    // leurs sauts de ligne (commandes à copier comprises).
    m_message = new QLabel;
    m_message->setTextFormat(Qt::PlainText);
    m_message->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_watched = new QLabel;
    m_watched->setTextFormat(Qt::PlainText);
    m_watched->setTextInteractionFlags(Qt::TextSelectableByMouse);
    Widgets::setSecondary(m_watched);

    auto *refreshButton = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), tr("Actualiser"));
    connect(refreshButton, &QPushButton::clicked, m_controller, &OnAccessController::refresh);

    auto *texts = new QVBoxLayout;
    texts->addWidget(m_title);
    texts->addWidget(m_message);
    texts->addWidget(m_watched);
    auto *cardLayout = new QHBoxLayout;
    cardLayout->addWidget(m_icon, 0, Qt::AlignTop);
    cardLayout->addLayout(texts, 1);
    cardLayout->addWidget(refreshButton, 0, Qt::AlignTop);
    m_card = new Card;
    m_card->setLayout(cardLayout);

    // Détections.
    auto *listTitle = new QLabel(tr("Détections"));
    listTitle->setFont(Widgets::scaledFont(listTitle->font(), 1.15, true));
    m_clearButton = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-clear-list")), tr("Effacer la liste"));
    connect(m_clearButton, &QPushButton::clicked, this, [this] {
        m_model->clear();
        emit detectionsCleared();
    });
    auto *listHeader = new QHBoxLayout;
    listHeader->addWidget(listTitle, 1);
    listHeader->addWidget(m_clearButton);
    auto *listNote = new QLabel(tr("Les fichiers détectés ne sont ni supprimés ni déplacés : "
                                   "l'application indique seulement leur emplacement."));
    listNote->setWordWrap(true);
    Widgets::setSecondary(listNote);

    m_view = new QTreeView;
    m_view->setModel(m_model);
    m_view->setRootIsDecorated(false);
    m_view->setUniformRowHeights(true);
    m_view->setAlternatingRowColors(true);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setTextElideMode(Qt::ElideMiddle);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QTreeView::customContextMenuRequested, this, &OnAccessPanel::showContextMenu);
    connect(m_view, &QTreeView::doubleClicked, this, [](const QModelIndex &index) {
        FileActions::showInFileManager(index.siblingAtColumn(OnAccessModel::PathColumn).data().toString());
    });
    QHeaderView *columns = m_view->header();
    columns->setStretchLastSection(false);
    columns->setSectionResizeMode(OnAccessModel::PathColumn, QHeaderView::Stretch);
    // Colonne « Heure » assez large pour ses textes réels (date au format local
    // ou « Avant le lancement »), plus une petite marge.
    const QFontMetrics metrics = fontMetrics();
    const int timeText = qMax(metrics.horizontalAdvance(QLocale().toString(QDateTime::currentDateTime(), QLocale::ShortFormat)),
                              metrics.horizontalAdvance(OnAccessModel::unknownTimeText()));
    columns->resizeSection(OnAccessModel::TimeColumn, timeText + 3 * metrics.averageCharWidth());
    columns->resizeSection(OnAccessModel::ThreatColumn, 36 * metrics.averageCharWidth());
    auto *viewStack = new PlaceholderStack(m_view, tr("Aucune menace détectée en temps réel."));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(Widgets::pageTitle(tr("Protection en temps réel")));
    layout->addWidget(m_card);
    layout->addSpacing(style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing));
    layout->addLayout(listHeader);
    layout->addWidget(listNote);
    layout->addWidget(viewStack, 1);

    connect(m_controller, &OnAccessController::stateChanged, this, &OnAccessPanel::updateState);
    connect(m_controller, &OnAccessController::historyLoaded, m_model, &OnAccessModel::setHistory);
    connect(m_controller, &OnAccessController::threatDetected, m_model, &OnAccessModel::addDetection);
    const auto updateClearButton = [this] { m_clearButton->setEnabled(m_model->rowCount() > 0); };
    connect(m_model, &QAbstractItemModel::rowsInserted, this, updateClearButton);
    connect(m_model, &QAbstractItemModel::modelReset, this, updateClearButton);
    updateClearButton();
    updateState();
}

void OnAccessPanel::updateState()
{
    const OnAccessController::State state = m_controller->state();
    m_card->setLevel(StatusDisplay::onAccessLevel(state));
    Widgets::setIcon(m_icon, StatusDisplay::onAccessIcon(state), style()->pixelMetric(QStyle::PM_LargeIconSize, nullptr, this));
    m_title->setText(StatusDisplay::onAccessTitle(state));
    m_message->setText(m_controller->message());
    m_message->setVisible(!m_controller->message().isEmpty());

    const QStringList watched = m_controller->watchedPaths();
    m_watched->setText(tr("Dossiers surveillés : %1\nConfiguration : %2")
                           .arg(watched.isEmpty() ? tr("aucun") : watched.join(QStringLiteral(", ")),
                                QString::fromLatin1(OnAccessController::kConfigPath)));
    // Sans clamonacc ni service, ces informations n'ont pas de sens.
    m_watched->setVisible(state != OnAccessController::State::NotInstalled
                          && state != OnAccessController::State::ServiceMissing);
}

void OnAccessPanel::showContextMenu(const QPoint &position)
{
    const QModelIndex index = m_view->indexAt(position);
    if (!index.isValid())
        return;
    FileActions::execContextMenu(this, m_view->viewport()->mapToGlobal(position),
                                 index.siblingAtColumn(OnAccessModel::PathColumn).data().toString(),
                                 index.siblingAtColumn(OnAccessModel::ThreatColumn).data().toString(),
                                 tr("Copier le nom de la menace"));
}
