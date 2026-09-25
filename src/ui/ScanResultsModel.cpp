#include "ScanResultsModel.h"

#include "StatusDisplay.h"
#include "core/ThreatText.h"

namespace
{
// Ordre de la liste triée par statut : ce qui demande une action d'abord.
int sortRank(ScanResult::Status status)
{
    switch (status) {
    case ScanResult::Status::Infected:
        return 0;
    case ScanResult::Status::Suspicious:
        return 1;
    case ScanResult::Status::Unscanned:
        return 2;
    case ScanResult::Status::Error:
        return 3;
    case ScanResult::Status::Clean:
        break;
    }
    return 4;
}

// Nom de signature : Infected, Suspicious, et Unscanned quand c'est clamd qui
// le signale (Heuristics.Encrypted...). Sinon, `detail` est déjà une explication.
bool hasSignature(const ScanResult &result)
{
    switch (result.status) {
    case ScanResult::Status::Infected:
    case ScanResult::Status::Suspicious:
        return true;
    case ScanResult::Status::Unscanned:
        return result.detail.startsWith(QLatin1String("Heuristics."));
    case ScanResult::Status::Clean:
    case ScanResult::Status::Error:
        break;
    }
    return false;
}
}

ScanResultsModel::ScanResultsModel(QObject *parent)
    : QAbstractTableModel(parent)
{
    for (int status = 0; status < ScanResult::kStatusCount; ++status)
        m_icons[status] = StatusDisplay::resultIcon(ScanResult::Status(status));
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
    case Qt::ToolTipRole: {
        QString tip = result.path;
        if (!result.detail.isEmpty())
            tip += QLatin1Char('\n') + result.detail;
        // Nom de signature : sa signification en clair (« Fichier chiffré... »).
        if (hasSignature(result)) {
            const QString description = ThreatText::describe(result.detail);
            if (description != result.detail)
                tip += QLatin1Char('\n') + description;
        }
        return tip;
    }
    case Qt::DecorationRole:
        if (index.column() == StatusColumn)
            return m_icons[int(result.status)];
        break;
    case Qt::FontRole:
        // Menace en gras : bien visible, sans couleur codée en dur.
        if (result.status == ScanResult::Status::Infected)
            return m_infectedFont;
        break;
    case StatusRole:
        return int(result.status);
    case SortRole:
        if (index.column() == StatusColumn)
            return sortRank(result.status);
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
        return tr("Détail");
    }
    return {};
}

ScanResultsFilter::ScanResultsFilter(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setSortRole(ScanResultsModel::SortRole);
    setDynamicSortFilter(true);
}

void ScanResultsFilter::setStatuses(const QList<ScanResult::Status> &statuses)
{
    if (statuses == m_statuses)
        return;
    m_statuses = statuses;
    invalidateFilter();
}

void ScanResultsFilter::setText(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed == m_text)
        return;
    m_text = trimmed;
    invalidateFilter();
}

bool ScanResultsFilter::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QAbstractItemModel *model = sourceModel();
    if (!m_statuses.isEmpty()
        && !m_statuses.contains(
            ScanResult::Status(model->index(sourceRow, 0, sourceParent).data(ScanResultsModel::StatusRole).toInt())))
        return false;
    if (m_text.isEmpty())
        return true;
    for (const int column : {ScanResultsModel::PathColumn, ScanResultsModel::DetailColumn}) {
        if (model->index(sourceRow, column, sourceParent).data().toString().contains(m_text, Qt::CaseInsensitive))
            return true;
    }
    return false;
}
