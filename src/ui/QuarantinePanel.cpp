#include "QuarantinePanel.h"

#include "StatusDisplay.h"
#include "Widgets.h"
#include "core/ThreatText.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace
{
enum Column { DateColumn, PathColumn, ThreatColumn, SizeColumn };
constexpr int kIdRole = Qt::UserRole;
}

QuarantinePanel::QuarantinePanel(Quarantine *quarantine, QWidget *parent)
    : QWidget(parent)
    , m_quarantine(quarantine)
{
    auto *note = new QLabel(tr("Les fichiers en quarantaine sont retirés de leur emplacement et rendus inertes : "
                               "leur contenu est brouillé, ils ne peuvent être ni ouverts ni exécutés, et ne sont "
                               "plus détectés. Ils restent restaurables. Dossier : %1")
                                .arg(QDir::toNativeSeparators(m_quarantine->directory())));
    note->setWordWrap(true);
    note->setTextInteractionFlags(Qt::TextSelectableByMouse);
    Widgets::setSecondary(note);

    m_message = new QLabel;
    m_message->setTextFormat(Qt::PlainText);
    m_message->setWordWrap(true);
    m_message->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_message->setVisible(false);

    m_restoreButton = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-undo")), tr("Restaurer…"));
    m_removeButton = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-delete")), tr("Supprimer définitivement…"));
    m_removeAllButton = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-clear-all")), tr("Tout supprimer…"));
    connect(m_restoreButton, &QPushButton::clicked, this, &QuarantinePanel::restoreSelected);
    connect(m_removeButton, &QPushButton::clicked, this, &QuarantinePanel::removeSelected);
    connect(m_removeAllButton, &QPushButton::clicked, this, &QuarantinePanel::removeAll);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_restoreButton);
    buttons->addWidget(m_removeButton);
    buttons->addStretch();
    buttons->addWidget(m_removeAllButton);

    m_list = new QTreeWidget;
    m_list->setColumnCount(4);
    m_list->setHeaderLabels({tr("Mis en quarantaine"), tr("Emplacement d'origine"), tr("Menace"), tr("Taille")});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setAlternatingRowColors(true);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setTextElideMode(Qt::ElideMiddle);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QTreeWidget::customContextMenuRequested, this, &QuarantinePanel::showContextMenu);
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, &QuarantinePanel::updateButtons);
    QHeaderView *header = m_list->header();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(PathColumn, QHeaderView::Stretch);
    const QFontMetrics metrics = fontMetrics();
    const int iconWidth = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
    header->resizeSection(DateColumn, iconWidth + metrics.horizontalAdvance(QLocale().toString(
                                                      QDateTime::currentDateTime(), QLocale::ShortFormat))
                                          + 4 * metrics.averageCharWidth());
    header->resizeSection(ThreatColumn, 32 * metrics.averageCharWidth());
    header->resizeSection(SizeColumn, 10 * metrics.averageCharWidth());
    m_listStack = new PlaceholderStack(m_list, tr("Aucun fichier en quarantaine.\nClic droit sur une menace "
                                                  "(analyse, protection en temps réel, historique) : « Mettre en "
                                                  "quarantaine »."));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(Widgets::pageTitle(tr("Quarantaine")));
    layout->addWidget(note);
    layout->addWidget(m_message);
    layout->addLayout(buttons);
    layout->addWidget(m_listStack, 1);

    connect(m_quarantine, &Quarantine::changed, this, &QuarantinePanel::reload);
    connect(m_quarantine, &Quarantine::finished, this, &QuarantinePanel::onFinished);
    connect(m_quarantine, &Quarantine::idle, this, &QuarantinePanel::updateButtons);
    reload();
}

void QuarantinePanel::reload()
{
    m_list->clear();
    const QIcon icon = StatusDisplay::quarantineIcon();
    for (const QuarantineEntry &entry : m_quarantine->entries()) {
        auto *item = new QTreeWidgetItem({QLocale().toString(entry.date, QLocale::ShortFormat), entry.originalPath,
                                          ThreatText::describe(entry.threat), QLocale().formattedDataSize(entry.size)});
        item->setIcon(DateColumn, icon);
        item->setData(DateColumn, kIdRole, entry.id);
        item->setToolTip(PathColumn, entry.originalPath);
        item->setToolTip(ThreatColumn, entry.threat);
        item->setToolTip(SizeColumn, tr("%1 octets\nSHA-256 : %2").arg(QLocale().toString(entry.size), entry.sha256));
        item->setTextAlignment(SizeColumn, Qt::AlignRight | Qt::AlignVCenter);
        m_list->addTopLevelItem(item);
    }
    updateButtons();
}

void QuarantinePanel::updateButtons()
{
    const bool busy = m_quarantine->isBusy();
    const bool selection = !m_list->selectedItems().isEmpty();
    m_restoreButton->setEnabled(selection && !busy);
    m_removeButton->setEnabled(selection && !busy);
    m_removeAllButton->setEnabled(m_list->topLevelItemCount() > 0 && !busy);
}

QList<QuarantineEntry> QuarantinePanel::selectedEntries() const
{
    QList<QuarantineEntry> selected;
    const QList<QuarantineEntry> entries = m_quarantine->entries();
    for (const QTreeWidgetItem *item : m_list->selectedItems()) {
        const QString id = item->data(DateColumn, kIdRole).toString();
        for (const QuarantineEntry &entry : entries) {
            if (entry.id == id)
                selected << entry;
        }
    }
    return selected;
}

void QuarantinePanel::restoreSelected()
{
    const QList<QuarantineEntry> entries = selectedEntries();
    if (entries.isEmpty())
        return;
    QStringList lines;
    for (const QuarantineEntry &entry : entries)
        lines << tr("%1 — %2").arg(entry.originalPath, ThreatText::describe(entry.threat));
    const QString question =
        tr("Restaurer à leur emplacement d'origine ?\n\n%1\n\nIls ont été détectés comme dangereux : restaurés, ils "
           "pourront de nouveau être ouverts ou exécutés, et la protection en temps réel les signalera sans doute "
           "encore. Ne le faites que pour un faux positif. Un fichier existant n'est jamais écrasé : le fichier "
           "restauré prend alors un autre nom.")
            .arg(lines.join(QLatin1Char('\n')));
    if (QMessageBox::warning(this, tr("Restaurer"), question, QMessageBox::Yes | QMessageBox::Cancel,
                             QMessageBox::Cancel)
        != QMessageBox::Yes)
        return;
    for (const QuarantineEntry &entry : entries)
        m_quarantine->restore(entry.id);
    updateButtons();
}

void QuarantinePanel::removeSelected()
{
    const QList<QuarantineEntry> entries = selectedEntries();
    if (entries.isEmpty())
        return;
    const QString question = entries.size() > 1
        ? tr("Supprimer définitivement ces %1 fichiers ? Impossible d'annuler.").arg(entries.size())
        : tr("Supprimer définitivement %1 ? Impossible d'annuler.").arg(entries.first().originalPath);
    if (QMessageBox::question(this, tr("Supprimer définitivement"), question) != QMessageBox::Yes)
        return;
    for (const QuarantineEntry &entry : entries)
        m_quarantine->remove(entry.id);
    updateButtons();
}

void QuarantinePanel::removeAll()
{
    const QList<QuarantineEntry> entries = m_quarantine->entries();
    if (entries.isEmpty()
        || QMessageBox::question(this, tr("Tout supprimer"),
                                 tr("Supprimer définitivement les %1 fichiers en quarantaine ? Impossible d'annuler.")
                                     .arg(entries.size()))
            != QMessageBox::Yes)
        return;
    for (const QuarantineEntry &entry : entries)
        m_quarantine->remove(entry.id);
    updateButtons();
}

void QuarantinePanel::showContextMenu(const QPoint &position)
{
    const QTreeWidgetItem *item = m_list->itemAt(position);
    if (!item)
        return;
    const QString id = item->data(DateColumn, kIdRole).toString();
    const QString path = item->text(PathColumn);
    QString sha256;
    for (const QuarantineEntry &entry : m_quarantine->entries()) {
        if (entry.id == id)
            sha256 = entry.sha256;
    }
    QMenu menu(this);
    menu.addAction(m_restoreButton->icon(), m_restoreButton->text(), this, &QuarantinePanel::restoreSelected)
        ->setEnabled(m_restoreButton->isEnabled());
    menu.addAction(m_removeButton->icon(), m_removeButton->text(), this, &QuarantinePanel::removeSelected)
        ->setEnabled(m_removeButton->isEnabled());
    menu.addSeparator();
    menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), tr("Copier l'emplacement d'origine"), this,
                   [path] { QApplication::clipboard()->setText(path); });
    // Empreinte : pour chercher le fichier sur VirusTotal ou MalwareBazaar, sans l'envoyer.
    if (!sha256.isEmpty())
        menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), tr("Copier l'empreinte SHA-256"), this,
                       [sha256] { QApplication::clipboard()->setText(sha256); });
    menu.exec(m_list->viewport()->mapToGlobal(position));
}

void QuarantinePanel::onFinished(Quarantine::Operation operation, const QString &path, const QString &error)
{
    QString text;
    if (!error.isEmpty())
        text = error;
    else if (operation == Quarantine::Operation::Restore)
        text = tr("Restauré : %1").arg(path);
    else if (operation == Quarantine::Operation::Add)
        text = tr("Mis en quarantaine : %1").arg(path);
    else
        text = tr("Supprimé définitivement : %1").arg(path);
    m_message->setText(text);
    m_message->setVisible(true);
    updateButtons();
}
