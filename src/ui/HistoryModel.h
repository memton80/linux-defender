#pragma once

#include "core/ScanHistory.h"

#include <QAbstractTableModel>
#include <QIcon>

/**
 * Liste des analyses de l'historique, la plus récente en tête. Suit les
 * modifications de l'historique (nouvelle analyse, effacement).
 */
class HistoryModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column { DateColumn, TypeColumn, TargetColumn, ScannedColumn, ThreatsColumn, ErrorsColumn, DurationColumn, ColumnCount };

    explicit HistoryModel(ScanHistory *history, QObject *parent = nullptr);

    ScanRecord record(int row) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    ScanHistory *m_history;
    QList<ScanRecord> m_records;
    QIcon m_icons[4]; // par StatusDisplay::Level
};
