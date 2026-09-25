#include "OnAccessModel.h"

#include "StatusDisplay.h"
#include "core/ThreatText.h"

#include <QLocale>

#include <algorithm>

OnAccessModel::OnAccessModel(QObject *parent)
    : QAbstractTableModel(parent)
{
    m_threatFont.setBold(true);
    m_icons[int(ThreatText::Kind::Threat)] = StatusDisplay::resultIcon(ScanResult::Status::Infected);
    m_icons[int(ThreatText::Kind::Suspicious)] = StatusDisplay::resultIcon(ScanResult::Status::Suspicious);
    m_icons[int(ThreatText::Kind::Unscanned)] = StatusDisplay::resultIcon(ScanResult::Status::Unscanned);
}

QString OnAccessModel::unknownTimeText()
{
    return tr("Avant le lancement");
}

void OnAccessModel::setHistory(const QList<OnAccessDetection> &detections)
{
    beginResetModel();
    m_detections = detections;
    std::reverse(m_detections.begin(), m_detections.end());
    if (m_detections.size() > kMaxRows)
        m_detections.resize(kMaxRows);
    endResetModel();
}

void OnAccessModel::addDetection(const OnAccessDetection &detection)
{
    beginInsertRows({}, 0, 0);
    m_detections.prepend(detection);
    endInsertRows();

    if (m_detections.size() > kMaxRows) {
        beginRemoveRows({}, kMaxRows, int(m_detections.size()) - 1);
        m_detections.resize(kMaxRows);
        endRemoveRows();
    }
}

void OnAccessModel::clear()
{
    beginResetModel();
    m_detections.clear();
    endResetModel();
}

int OnAccessModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_detections.size());
}

int OnAccessModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant OnAccessModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_detections.size())
        return {};
    const OnAccessDetection &detection = m_detections.at(index.row());

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case TimeColumn:
            return detection.time.isValid() ? QLocale().toString(detection.time, QLocale::ShortFormat)
                                            : unknownTimeText();
        case PathColumn:
            return detection.path;
        case ThreatColumn:
            return detection.threat;
        }
        break;
    case Qt::ToolTipRole:
        return tr("%1\n%2 (%3)\nLe fichier n'a été ni supprimé ni déplacé.")
            .arg(detection.path, ThreatText::describe(detection.threat), detection.threat);
    case Qt::DecorationRole:
        if (index.column() == ThreatColumn)
            return m_icons[int(ThreatText::kind(detection.threat))];
        break;
    case Qt::FontRole:
        // En gras : les vraies menaces seulement.
        if (index.column() == ThreatColumn && ThreatText::kind(detection.threat) == ThreatText::Kind::Threat)
            return m_threatFont;
        break;
    }
    return {};
}

QVariant OnAccessModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case TimeColumn:
        return tr("Heure");
    case PathColumn:
        return tr("Fichier");
    case ThreatColumn:
        return tr("Détection");
    }
    return {};
}
