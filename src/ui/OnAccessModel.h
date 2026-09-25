#pragma once

#include "system/OnAccessLog.h"

#include <QAbstractTableModel>
#include <QFont>
#include <QIcon>

/**
 * Liste des détections en temps réel (clamonacc), la plus récente en tête.
 * Distincte des résultats des scans manuels. Bornée à kMaxRows détections.
 */
class OnAccessModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column { TimeColumn, PathColumn, ThreatColumn, ColumnCount };
    static constexpr int kMaxRows = 1000;

    explicit OnAccessModel(QObject *parent = nullptr);

    // Texte de la colonne « Heure » pour une détection lue au lancement
    // (clamonacc n'horodate pas son journal).
    static QString unknownTimeText();

    // Détections déjà présentes dans le journal au lancement (dans l'ordre du journal).
    void setHistory(const QList<OnAccessDetection> &detections);
    void addDetection(const OnAccessDetection &detection);
    void clear();

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    QList<OnAccessDetection> m_detections; // la plus récente en premier
    QFont m_threatFont;
    QIcon m_icons[3]; // par ThreatText::Kind
};
