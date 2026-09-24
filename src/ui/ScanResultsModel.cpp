#include "ScanResultsModel.h"

#include "StatusDisplay.h"

ScanResultsModel::ScanResultsModel(QObject *parent)
    : QAbstractTableModel(parent)
    , m_cleanIcon(StatusDisplay::resultIcon(ScanResult::Status::Clean))
    , m_infectedIcon(StatusDisplay::resultIcon(ScanResult::Status::Infected))
    , m_errorIcon(StatusDisplay::resultIcon(ScanResult::Status::Error))
{
    m_infectedFont.setBold(true);
}

void ScanResultsModel::clear()
{
    beginResetModel();
    m_results.clear();
    m_cleanRows = 0;
    m_unlistedClean = 0;
    endResetModel();
}

void ScanResultsModel::append(const QList<ScanResult> &results)
{
    QList<ScanResult> listed;
    listed.reserve(results.size());
    for (const ScanResult &result : results) {
        if (result.status == ScanResult::Status::Clean) {
            if (m_cleanRows >= kMaxCleanRows) {
                ++m_unlistedClean;
                continue;
            }
            ++m_cleanRows;
        }
        listed.append(result);
    }
    if (listed.isEmpty())
        return;

    const int first = int(m_results.size());
    beginInsertRows({}, first, first + int(listed.size()) - 1);
    m_results.append(listed);
    endInsertRows();
}

qint64 ScanResultsModel::unlistedCleanCount() const
{
    return m_unlistedClean;
}

int ScanResultsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_results.size());
}

int ScanResultsModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant ScanResultsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_results.size())
        return {};
    const ScanResult &result = m_results.at(index.row());

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case StatusColumn:
            return StatusDisplay::resultText(result.status);
        case PathColumn:
            return result.path;
        case DetailColumn:
            return result.detail;
        }
        break;
    case Qt::ToolTipRole:
        return result.detail.isEmpty() ? result.path : result.path + QLatin1Char('\n') + result.detail;
    case Qt::DecorationRole:
        if (index.column() == StatusColumn) {
            switch (result.status) {
            case ScanResult::Status::Clean:
                return m_cleanIcon;
            case ScanResult::Status::Infected:
                return m_infectedIcon;
            case ScanResult::Status::Error:
                return m_errorIcon;
            }
        }
        break;
    case Qt::FontRole:
        // Menace en gras : bien visible, sans couleur codée en dur.
        if (result.status == ScanResult::Status::Infected)
            return m_infectedFont;
        break;
    case StatusRole:
        return int(result.status);
    case SortRole:
        if (index.column() == StatusColumn) {
            switch (result.status) {
            case ScanResult::Status::Infected:
                return 0;
            case ScanResult::Status::Error:
                return 1;
            case ScanResult::Status::Clean:
                return 2;
            }
        }
        return data(index, Qt::DisplayRole);
    }
    return {};
}

QVariant ScanResultsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case StatusColumn:
        return tr("Statut");
    case PathColumn:
        return tr("Fichier");
    case DetailColumn:
        return tr("Menace ou erreur");
    }
    return {};
}
