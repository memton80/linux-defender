#pragma once

#include "system/PrivilegedHelper.h"

#include <QWidget>

class Card;
class QCheckBox;
class Quarantine;
class OnAccessController;
class OnAccessModel;
class QLabel;
class QPushButton;
class QTreeView;

/**
 * Page « Protection en temps réel » de la fenêtre principale : état du
 * service clamonacc (avec le diagnostic en cas de problème), case pour
 * l'activer ou la désactiver (programme d'aide, mot de passe administrateur),
 * dossiers surveillés et liste des détections en temps réel.
 */
class OnAccessPanel : public QWidget
{
    Q_OBJECT

public:
    OnAccessPanel(OnAccessController *controller, PrivilegedHelper *helper, Quarantine *quarantine,
                  QWidget *parent = nullptr);

signals:
    // L'utilisateur a effacé la liste des détections.
    void detectionsCleared();

private:
    void updateState();
    void showContextMenu(const QPoint &position);
    void onToggled(bool enable);
    void onHelperFinished(PrivilegedHelper::Action action, PrivilegedHelper::Result result, const QString &message);

    OnAccessController *m_controller;
    PrivilegedHelper *m_helper;
    Quarantine *m_quarantine;
    QCheckBox *m_toggle;
    QLabel *m_toggleMessage;
    bool m_toggling = false; // action lancée par la case, résultat attendu
    OnAccessModel *m_model;
    Card *m_card;
    QLabel *m_icon;
    QLabel *m_title;
    QLabel *m_message;
    QLabel *m_watched;
    QPushButton *m_clearButton;
    QTreeView *m_view;
};
