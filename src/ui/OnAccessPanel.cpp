#include "OnAccessPanel.h"

#include "OnAccessModel.h"
#include "StatusDisplay.h"
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
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    m_message = new QLabel;
    m_message->setTextFormat(Qt::PlainText);
    m_message->setTextInteractionFlags(Qt::TextSelectableByMouse); // commandes à copier

    auto *texts = new QVBoxLayout;
    texts->addWidget(m_title);
    texts->addWidget(m_message);
    auto *header = new QHBoxLayout;
    header->addWidget(m_icon, 0, Qt::AlignTop);
    header->addLayout(texts, 1);

    // Détections.
    auto *listTitle = new QLabel(tr("Détections en temps réel (les fichiers ne sont ni supprimés ni déplacés) :"));
    auto *clearButton = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-clear-list")), tr("Effacer la liste"));
    connect(clearButton, &QPushButton::clicked, m_model, &OnAccessModel::clear);
    auto *listHeader = new QHBoxLayout;
    listHeader->addWidget(listTitle, 1);
    listHeader->addWidget(clearButton);

    m_view = new QTreeView;
    m_view->setModel(m_model);
    m_view->setRootIsDecorated(false);
    m_view->setUniformRowHeights(true);
    m_view->setAlternatingRowColors(true);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setTextElideMode(Qt::ElideMiddle);
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

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(header);
    layout->addLayout(listHeader);
    layout->addWidget(m_view, 1);

    connect(m_controller, &OnAccessController::stateChanged, this, &OnAccessPanel::updateState);
    connect(m_controller, &OnAccessController::historyLoaded, m_model, &OnAccessModel::setHistory);
    connect(m_controller, &OnAccessController::threatDetected, m_model, &OnAccessModel::addDetection);
    updateState();
}

void OnAccessPanel::updateState()
{
    const OnAccessController::State state = m_controller->state();
    const int size = style()->pixelMetric(QStyle::PM_LargeIconSize, nullptr, this);
    m_icon->setPixmap(StatusDisplay::onAccessIcon(state).pixmap(QSize(size, size), devicePixelRatioF()));
    m_title->setText(StatusDisplay::onAccessTitle(state));
    m_message->setText(m_controller->message());
    m_message->setVisible(!m_controller->message().isEmpty());
}
