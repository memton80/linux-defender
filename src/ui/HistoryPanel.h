#pragma once

#include <QWidget>

class Card;
class HistoryModel;
class PlaceholderStack;
class QLabel;
class QPushButton;
class QTreeView;
class QTreeWidget;
class ScanHistory;

/**
 * Page « Historique » : analyses passées (date, type, cible, compteurs) et,
 * pour l'analyse choisie, son bilan et les menaces trouvées.
 */
class HistoryPanel : public QWidget
{
    Q_OBJECT

public:
    explicit HistoryPanel(ScanHistory *history, QWidget *parent = nullptr);

    // Sélectionne l'analyse la plus récente.
    void selectLatest();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void showDetails();
    void updateInfo();
    void clearHistory();

    ScanHistory *m_history;
    HistoryModel *m_model;
    QLabel *m_info;
    QPushButton *m_clearButton;
    QTreeView *m_view;
    PlaceholderStack *m_viewStack;
    Card *m_details;
    QLabel *m_detailsIcon;
    QLabel *m_detailsTitle;
    QLabel *m_detailsText;
    QLabel *m_threatsTitle;
    QTreeWidget *m_threats;
};
