#include "Widgets.h"

#include <QAbstractItemView>
#include <QDir>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QHBoxLayout>
#include <QLabel>
#include <QHeaderView>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace
{
constexpr qreal kCornerRadius = 6;
}

Card::Card(QWidget *parent)
    : QFrame(parent)
{
    setFrameShape(QFrame::NoFrame);
}

StatusDisplay::Level Card::level() const
{
    return m_level;
}

void Card::setLevel(StatusDisplay::Level level)
{
    if (level == m_level)
        return;
    m_level = level;
    update();
}

void Card::paintEvent(QPaintEvent *)
{
    QColor fill = StatusDisplay::levelColor(m_level);
    QColor border = fill;
    if (fill.isValid()) {
        fill.setAlphaF(0.12);
        border.setAlphaF(0.55);
    } else {
        fill = border = palette().color(QPalette::WindowText);
        fill.setAlphaF(0.04);
        border.setAlphaF(0.15);
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(border, 1));
    painter.setBrush(fill);
    // Décalage d'un demi-pixel : trait net, entièrement dans le widget.
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), kCornerRadius, kCornerRadius);
}

PlaceholderStack::PlaceholderStack(QAbstractItemView *view, const QString &placeholder, QWidget *parent)
    : QStackedWidget(parent)
    , m_view(view)
{
    m_placeholder = new QLabel(placeholder);
    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->setWordWrap(true);
    Widgets::setSecondary(m_placeholder);

    // Même cadre et même fond que la liste : seul le contenu change.
    auto *frame = new QFrame;
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setFrameShadow(QFrame::Sunken);
    frame->setBackgroundRole(QPalette::Base);
    frame->setAutoFillBackground(true);
    auto *layout = new QVBoxLayout(frame);
    layout->addWidget(m_placeholder);

    addWidget(frame);
    addWidget(m_view);

    QAbstractItemModel *model = m_view->model();
    Q_ASSERT(model);
    connect(model, &QAbstractItemModel::rowsInserted, this, &PlaceholderStack::refresh);
    connect(model, &QAbstractItemModel::rowsRemoved, this, &PlaceholderStack::refresh);
    connect(model, &QAbstractItemModel::modelReset, this, &PlaceholderStack::refresh);
    connect(model, &QAbstractItemModel::layoutChanged, this, &PlaceholderStack::refresh);
    refresh();
}

PlaceholderStack::~PlaceholderStack()
{
    // La liste (enfant) est détruite après : un QTreeWidget vide alors son
    // modèle, dont les signaux ne doivent plus arriver ici.
    disconnect(m_view->model(), nullptr, this, nullptr);
}

void PlaceholderStack::setPlaceholderText(const QString &text)
{
    m_placeholder->setText(text);
}

void PlaceholderStack::refresh()
{
    setCurrentIndex(m_view->model()->rowCount() > 0 ? 1 : 0);
}

PathListEdit::PathListEdit(Mode mode, QWidget *parent)
    : QWidget(parent)
{
    // Liste à une colonne sans en-tête : un chemin trop long est coupé au
    // milieu (début du chemin et nom gardés), sans barre de défilement horizontale.
    m_list = new QTreeWidget;
    m_list->setHeaderHidden(true);
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setTextElideMode(Qt::ElideMiddle);
    m_list->header()->setStretchLastSection(true);
    // Hauteur de quatre lignes environ : la page garde de la place pour le reste.
    m_list->setFixedHeight(4 * (fontMetrics().height() + style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this) / 2)
                           + 2 * m_list->frameWidth());
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, &PathListEdit::updateButtons);

    auto *addFolder = new QPushButton(QIcon::fromTheme(QStringLiteral("folder-new")), tr("Ajouter un dossier…"));
    connect(addFolder, &QPushButton::clicked, this, [this] {
        const QString folder = QFileDialog::getExistingDirectory(this, tr("Dossier à ajouter"), QDir::homePath());
        if (!folder.isEmpty() && addPath(folder))
            emit changed();
    });
    m_removeButton = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")), tr("Retirer"));
    connect(m_removeButton, &QPushButton::clicked, this, [this] {
        qDeleteAll(m_list->selectedItems());
        updateButtons();
        emit changed();
    });

    auto *buttons = new QVBoxLayout;
    buttons->addWidget(addFolder);
    if (mode == Mode::FoldersAndFiles) {
        auto *addFiles = new QPushButton(QIcon::fromTheme(QStringLiteral("document-new")), tr("Ajouter des fichiers…"));
        connect(addFiles, &QPushButton::clicked, this, [this] {
            bool added = false;
            for (const QString &file : QFileDialog::getOpenFileNames(this, tr("Fichiers à ajouter"), QDir::homePath()))
                added = addPath(file) || added;
            if (added)
                emit changed();
        });
        buttons->addWidget(addFiles);
    }
    buttons->addWidget(m_removeButton);
    buttons->addStretch();

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_list, 1);
    layout->addLayout(buttons);
    updateButtons();
}

QStringList PathListEdit::paths() const
{
    QStringList paths;
    for (int row = 0; row < m_list->topLevelItemCount(); ++row)
        paths << m_list->topLevelItem(row)->text(0);
    return paths;
}

void PathListEdit::setPaths(const QStringList &paths)
{
    m_list->clear();
    for (const QString &path : paths)
        addPath(path);
    updateButtons();
}

bool PathListEdit::addPath(const QString &path)
{
    const QString clean = QDir::cleanPath(path);
    if (clean.isEmpty() || !m_list->findItems(clean, Qt::MatchExactly).isEmpty())
        return false;
    auto *item = new QTreeWidgetItem(m_list, {clean});
    item->setIcon(0, QFileIconProvider().icon(QFileInfo(clean)));
    item->setToolTip(0, clean);
    return true;
}

void PathListEdit::updateButtons()
{
    m_removeButton->setEnabled(!m_list->selectedItems().isEmpty());
}

namespace Widgets
{

QFont scaledFont(const QFont &font, qreal factor, bool bold)
{
    QFont scaled = font;
    scaled.setPointSizeF(font.pointSizeF() * factor);
    scaled.setBold(bold);
    return scaled;
}

QLabel *pageTitle(const QString &text)
{
    auto *label = new QLabel(text);
    label->setFont(scaledFont(label->font(), 1.5, true));
    return label;
}

void setSecondary(QWidget *widget)
{
    widget->setForegroundRole(QPalette::PlaceholderText);
}

void setIcon(QLabel *label, const QIcon &icon, int size)
{
    label->setPixmap(icon.pixmap(QSize(size, size), label->devicePixelRatioF()));
    label->setFixedSize(size, size);
}

} // namespace Widgets
