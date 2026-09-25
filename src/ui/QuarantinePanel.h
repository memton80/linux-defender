#pragma once

#include "core/Quarantine.h"

#include <QWidget>

class PlaceholderStack;
class QLabel;
class QPushButton;
class QTreeWidget;

/**
 * Page « Quarantaine » : fichiers retirés de leur emplacement et rendus
 * inertes, avec leur menace, leur date et leur empreinte ; restauration (avec
 * avertissement) et suppression définitive (avec confirmation).
 */
class QuarantinePanel : public QWidget
{
    Q_OBJECT

public:
    explicit QuarantinePanel(Quarantine *quarantine, QWidget *parent = nullptr);

private:
    void reload();
    void updateButtons();
    QList<QuarantineEntry> selectedEntries() const;
    void restoreSelected();
    void removeSelected();
    void removeAll();
    void showContextMenu(const QPoint &position);
    void onFinished(Quarantine::Operation operation, const QString &path, const QString &error);

    Quarantine *m_quarantine;
    QLabel *m_message;
    QPushButton *m_restoreButton;
    QPushButton *m_removeButton;
    QPushButton *m_removeAllButton;
    QTreeWidget *m_list;
    PlaceholderStack *m_listStack;
};
