#include "HistoryModel.h"

#include "StatusDisplay.h"

#include <QLocale>

HistoryModel::HistoryModel(ScanHistory *history, QObject *parent)
    : QAbstractTableModel(parent)
    , m_history(history)
    , m_records(history->records())
{
    for (const StatusDisplay::Level level : {StatusDisplay::Level::Neutral, StatusDisplay::Level::Positive,
                                              StatusDisplay::Level::Warning, StatusDisplay::Level::Negative})
        m_icons[int(level)] = StatusDisplay::levelIcon(level);
    m_icons[int(StatusDisplay::Level::Negative)] = StatusDisplay::threatIcon();

    connect(m_history, &ScanHistory::changed, this, [this] {
        beginResetModel();
        m_records = m_history->records();
        endResetModel();
    });
}

ScanRecord HistoryModel::record(int row) const
{
    return m_records.value(row);
}

int HistoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_records.size());
}

int HistoryModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant HistoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_records.size())
        return {};
    const ScanRecord &record = m_records.at(index.row());

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case DateColumn:
            return QLocale().toString(record.started, QLocale::ShortFormat);
        case TypeColumn:
            return StatusDisplay::originText(record.origin);
        case TargetColumn:
            return StatusDisplay::targetText(record.origin, record.paths);
        case ScannedColumn:
            return QLocale().toString(record.scanned);
        case ThreatsColumn:
            return QLocale().toString(record.infected);
        case WarningsColumn:
            return QLocale().toString(record.suspicious + record.unscanned);
        case ErrorsColumn:
            return QLocale().toString(record.errors);
        case DurationColumn:
            return StatusDisplay::durationText(record.elapsedMsecs);
        }
        break;
    case Qt::DecorationRole:
        if (index.column() == DateColumn)
            return m_icons[int(StatusDisplay::summaryLevel(record.toSummary()))];
        break;
    case Qt::ToolTipRole:
        return record.paths.join(QLatin1Char('\n')) + QLatin1Char('\n') + StatusDisplay::summaryText(record.toSummary());
    case Qt::TextAlignmentRole:
        if (index.column() >= ScannedColumn)
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        break;
    }
    return {};
}

QVariant HistoryModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case DateColumn:
        return tr("Date");
    case TypeColumn:
        return tr("Type");
    case TargetColumn:
        return tr("Cible");
    case ScannedColumn:
        return tr("Fichiers");
    case ThreatsColumn:
        return tr("Menaces");
    case WarningsColumn:
        return tr("Avertissements");
    case ErrorsColumn:
        return tr("Erreurs");
    case DurationColumn:
        return tr("Durée");
    }
    return {};
}
