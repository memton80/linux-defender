#pragma once

#include <QWidget>

class OnAccessController;
class OnAccessModel;
class QLabel;
class QTreeView;

/**
 * Onglet « Protection en temps réel » de la fenêtre principale : état du
 * service clamonacc (avec le diagnostic en cas de problème) et liste des
 * détections en temps réel.
 */
class OnAccessPanel : public QWidget
{
    Q_OBJECT

public:
    explicit OnAccessPanel(OnAccessController *controller, QWidget *parent = nullptr);

private:
    void updateState();

    OnAccessController *m_controller;
    OnAccessModel *m_model;
    QLabel *m_icon;
    QLabel *m_title;
    QLabel *m_message;
    QTreeView *m_view;
};
