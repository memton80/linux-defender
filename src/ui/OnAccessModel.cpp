#include "OnAccessModel.h"

#include "StatusDisplay.h"
#include "core/Quarantine.h"
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
    m_quarantineIcon = StatusDisplay::quarantineIcon();
}

void OnAccessModel::setQuarantine(const Quarantine *quarantine)
{
    m_quarantine = quarantine;
    connect(quarantine, &Quarantine::changed, this, [this] {
        if (!m_detections.isEmpty())
            emit dataChanged(index(0, 0), index(int(m_detections.size()) - 1, ColumnCount - 1));
    });
}

OnAccessDetection OnAccessModel::detection(int row) const
{
    return m_detections.value(row);
}

bool OnAccessModel::isQuarantined(int row) const
{
    return row >= 0 && row < m_detections.size() && m_quarantine
        && m_quarantine->contains(m_detections.at(row).path);
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
            return isQuarantined(index.row()) ? tr("%1 — en quarantaine").arg(detection.threat) : detection.threat;
        }
        break;
    case Qt::ToolTipRole:
        return tr("%1\n%2 (%3)\nLe fichier n'a été ni supprimé ni déplacé.")
            .arg(detection.path, ThreatText::describe(detection.threat), detection.threat);
    case Qt::DecorationRole:
        if (index.column() == ThreatColumn)
            return isQuarantined(index.row()) ? m_quarantineIcon : m_icons[int(ThreatText::kind(detection.threat))];
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
