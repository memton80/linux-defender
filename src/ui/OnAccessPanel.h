#pragma once

#include <QWidget>

class Card;
class OnAccessController;
class OnAccessModel;
class QLabel;
class QPushButton;
class QTreeView;

/**
 * Page « Protection en temps réel » de la fenêtre principale : état du
 * service clamonacc (avec le diagnostic en cas de problème), dossiers
 * surveillés et liste des détections en temps réel.
 */
class OnAccessPanel : public QWidget
{
    Q_OBJECT

public:
    explicit OnAccessPanel(OnAccessController *controller, QWidget *parent = nullptr);

signals:
    // L'utilisateur a effacé la liste des détections.
    void detectionsCleared();

private:
    void updateState();
    void showContextMenu(const QPoint &position);

    OnAccessController *m_controller;
    OnAccessModel *m_model;
    Card *m_card;
    QLabel *m_icon;
    QLabel *m_title;
    QLabel *m_message;
    QLabel *m_watched;
    QPushButton *m_clearButton;
    QTreeView *m_view;
};
