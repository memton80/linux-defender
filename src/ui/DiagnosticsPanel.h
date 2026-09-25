#pragma once

#include "system/PrivilegedHelper.h"
#include "system/SystemDiagnostics.h"

#include <QHash>
#include <QWidget>

class Card;
class QLabel;
class QPushButton;
class QVBoxLayout;

/**
 * Page « Diagnostic » : une carte par vérification (clamd, accès au socket,
 * signatures, SELinux, limites d'analyse, protection en temps réel), avec
 * son explication, la commande exacte à lancer et, si le programme d'aide
 * est installé, un bouton qui applique la correction (mot de passe
 * administrateur demandé par polkit).
 */
class DiagnosticsPanel : public QWidget
{
    Q_OBJECT

public:
    DiagnosticsPanel(SystemDiagnostics *diagnostics, PrivilegedHelper *helper, QWidget *parent = nullptr);

signals:
    // Une correction a réussi : l'application relit la configuration de
    // clamd (socket) et l'état des services.
    void fixApplied();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void rebuild();
    QWidget *createItemCard(const DiagnosticItem &item);
    void onFixFinished(PrivilegedHelper::Action action, PrivilegedHelper::Result result, const QString &message);

    SystemDiagnostics *m_diagnostics;
    PrivilegedHelper *m_helper;
    Card *m_summary;
    QLabel *m_summaryIcon;
    QLabel *m_summaryTitle;
    QLabel *m_summaryText;
    QPushButton *m_refreshButton;
    QVBoxLayout *m_itemsLayout;
    QString m_runningItem;               // vérification dont la correction est en cours
    QHash<QString, QString> m_messages;  // résultat de la dernière correction, par vérification
};
