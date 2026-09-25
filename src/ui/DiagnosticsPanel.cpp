#include "DiagnosticsPanel.h"

#include "StatusDisplay.h"
#include "Widgets.h"

#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
QLabel *wrappedLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}
}

DiagnosticsPanel::DiagnosticsPanel(SystemDiagnostics *diagnostics, PrivilegedHelper *helper, QWidget *parent)
    : QWidget(parent)
    , m_diagnostics(diagnostics)
    , m_helper(helper)
{
    // Bilan : nombre de problèmes, disponibilité des corrections automatiques.
    m_summaryIcon = new QLabel;
    m_summaryTitle = new QLabel;
    m_summaryTitle->setFont(Widgets::scaledFont(m_summaryTitle->font(), 1.15, true));
    m_summaryText = wrappedLabel(QString());
    m_refreshButton = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), tr("Vérifier à nouveau"));
    connect(m_refreshButton, &QPushButton::clicked, m_diagnostics, &SystemDiagnostics::refresh);
    auto *summaryTexts = new QVBoxLayout;
    summaryTexts->addWidget(m_summaryTitle);
    summaryTexts->addWidget(m_summaryText);
    auto *summaryLayout = new QHBoxLayout;
    summaryLayout->addWidget(m_summaryIcon, 0, Qt::AlignTop);
    summaryLayout->addLayout(summaryTexts, 1);
    summaryLayout->addWidget(m_refreshButton, 0, Qt::AlignTop);
    m_summary = new Card;
    m_summary->setLayout(summaryLayout);

    // Vérifications, dans une zone qui défile.
    auto *items = new QWidget;
    m_itemsLayout = new QVBoxLayout(items);
    m_itemsLayout->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea;
    scroll->setWidget(items);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(Widgets::pageTitle(tr("Diagnostic")));
    layout->addWidget(m_summary);
    layout->addSpacing(style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing));
    layout->addWidget(scroll, 1);

    connect(m_diagnostics, &SystemDiagnostics::changed, this, &DiagnosticsPanel::rebuild);
    // Boutons grisés pendant toute action du programme d'aide, d'où qu'elle vienne.
    connect(m_helper, &PrivilegedHelper::started, this, &DiagnosticsPanel::rebuild);
    connect(m_helper, &PrivilegedHelper::finished, this, &DiagnosticsPanel::onFixFinished);
    rebuild();
}

void DiagnosticsPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_diagnostics->refresh();
}

void DiagnosticsPanel::rebuild()
{
    QList<DiagnosticItem> items = m_diagnostics->items();
    // Problèmes d'abord, du plus grave au moins grave ; ordre d'origine sinon.
    std::stable_sort(items.begin(), items.end(),
                     [](const DiagnosticItem &a, const DiagnosticItem &b) { return a.level > b.level; });

    const auto problems = std::count_if(items.cbegin(), items.cend(), [](const DiagnosticItem &item) {
        return item.level >= DiagnosticItem::Level::Warning;
    });
    const auto suggestions = std::count_if(items.cbegin(), items.cend(), [](const DiagnosticItem &item) {
        return item.level == DiagnosticItem::Level::Info;
    });
    const DiagnosticItem::Level worst = m_diagnostics->worstLevel();
    m_summary->setLevel(items.isEmpty() ? StatusDisplay::Level::Neutral : StatusDisplay::diagnosticLevel(worst));
    Widgets::setIcon(m_summaryIcon, StatusDisplay::diagnosticIcon(items.isEmpty() ? DiagnosticItem::Level::Info : worst),
                     style()->pixelMetric(QStyle::PM_LargeIconSize, nullptr, this));
    if (items.isEmpty())
        m_summaryTitle->setText(tr("Vérification en cours…"));
    else if (problems > 1)
        m_summaryTitle->setText(tr("%1 problèmes à corriger").arg(problems));
    else if (problems == 1)
        m_summaryTitle->setText(tr("Un problème à corriger"));
    else if (suggestions > 0)
        m_summaryTitle->setText(tr("Aucun problème, des améliorations possibles"));
    else
        m_summaryTitle->setText(tr("Aucun problème détecté"));
    m_summaryText->setText(
        m_helper->isAvailable()
            ? tr("Les boutons de correction demandent le mot de passe administrateur (une seule fois pour quelques "
                 "minutes). Chaque commande peut aussi être copiée et lancée dans un terminal.")
            : tr("Corrections automatiques indisponibles : le programme d'aide est installé par les paquets .deb "
                 "et .rpm (pas par l'archive .tar.gz), et demande pkexec. Copiez les commandes proposées dans un "
                 "terminal."));

    // Cartes recréées à chaque changement : il y en a peu. Suppression
    // différée : le bouton cliqué qui a provoqué la reconstruction est dans l'une d'elles.
    while (QLayoutItem *child = m_itemsLayout->takeAt(0)) {
        if (QWidget *widget = child->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete child;
    }
    for (const DiagnosticItem &item : std::as_const(items))
        m_itemsLayout->addWidget(createItemCard(item));
    m_itemsLayout->addStretch();
}

QWidget *DiagnosticsPanel::createItemCard(const DiagnosticItem &item)
{
    auto *icon = new QLabel;
    Widgets::setIcon(icon, StatusDisplay::diagnosticIcon(item.level),
                     style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this));
    auto *title = new QLabel(item.title);
    title->setTextFormat(Qt::PlainText);
    title->setFont(Widgets::scaledFont(title->font(), 1, true));
    auto *header = new QHBoxLayout;
    header->addWidget(icon);
    header->addWidget(title, 1);

    auto *card = new Card;
    // Seuls les problèmes sont colorés : le reste de la page reste calme.
    card->setLevel(item.level >= DiagnosticItem::Level::Warning ? StatusDisplay::diagnosticLevel(item.level)
                                                                : StatusDisplay::Level::Neutral);
    auto *layout = new QVBoxLayout(card);
    layout->addLayout(header);
    if (!item.text.isEmpty())
        layout->addWidget(wrappedLabel(item.text));

    if (!item.command.isEmpty()) {
        auto *command = wrappedLabel(item.command);
        command->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        command->setFrameShape(QFrame::StyledPanel);
        command->setBackgroundRole(QPalette::Base);
        command->setAutoFillBackground(true);
        command->setMargin(style()->pixelMetric(QStyle::PM_LayoutLeftMargin) / 2);
        auto *copy = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-copy")), tr("Copier"));
        copy->setToolTip(tr("Copier la commande, à coller dans un terminal"));
        const QString text = item.command;
        connect(copy, &QPushButton::clicked, this, [text] { QApplication::clipboard()->setText(text); });
        auto *commandRow = new QHBoxLayout;
        commandRow->addWidget(command, 1);
        commandRow->addWidget(copy, 0, Qt::AlignTop);
        layout->addLayout(commandRow);
    }

    if (item.fix && m_helper->isAvailable()) {
        const bool running = m_runningItem == item.id;
        auto *fix = new QPushButton(QIcon::fromTheme(QStringLiteral("tools-wizard"),
                                                     QIcon::fromTheme(QStringLiteral("dialog-password"))),
                                    running ? tr("Correction en cours…") : item.fixLabel);
        fix->setEnabled(!m_helper->isRunning());
        const QString id = item.id;
        const PrivilegedHelper::Action action = *item.fix;
        const QString argument = item.fixArgument;
        connect(fix, &QPushButton::clicked, this, [this, id, action, argument] {
            if (m_helper->isRunning())
                return; // action de la case « Protection en temps réel » en cours
            m_runningItem = id;
            m_messages.remove(id);
            m_helper->run(action, argument);
            rebuild();
        });
        auto *fixRow = new QHBoxLayout;
        fixRow->addWidget(fix);
        fixRow->addStretch();
        layout->addLayout(fixRow);
    }

    const QString message = m_messages.value(item.id);
    if (!message.isEmpty()) {
        auto *result = wrappedLabel(message);
        Widgets::setSecondary(result);
        layout->addWidget(result);
    }
    return card;
}

void DiagnosticsPanel::onFixFinished(PrivilegedHelper::Action, PrivilegedHelper::Result result, const QString &message)
{
    if (m_runningItem.isEmpty()) {
        rebuild(); // action lancée ailleurs (case de la protection en temps réel) : boutons réactivés
        return;
    }
    const QString id = m_runningItem;
    m_runningItem.clear();
    switch (result) {
    case PrivilegedHelper::Result::Success:
        m_messages.insert(id, tr("Correction appliquée. Si le problème persiste, le diagnostic indique la suite."));
        emit fixApplied();
        break;
    case PrivilegedHelper::Result::Cancelled:
    case PrivilegedHelper::Result::Failed:
        m_messages.insert(id, message);
        break;
    }
    m_diagnostics->refresh();
    rebuild();
}
