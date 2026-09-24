#include "ScanHistory.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

namespace
{
constexpr int kFormatVersion = 1;

// Noms enregistrés dans le fichier : indépendants des valeurs de l'énumération.
QString originKey(ScanManager::Origin origin)
{
    switch (origin) {
    case ScanManager::Origin::Usb:
        return QStringLiteral("usb");
    case ScanManager::Origin::Quick:
        return QStringLiteral("quick");
    case ScanManager::Origin::Full:
        return QStringLiteral("full");
    case ScanManager::Origin::Manual:
        break;
    }
    return QStringLiteral("manual");
}

ScanManager::Origin originFromKey(const QString &key)
{
    if (key == QLatin1String("usb"))
        return ScanManager::Origin::Usb;
    if (key == QLatin1String("quick"))
        return ScanManager::Origin::Quick;
    if (key == QLatin1String("full"))
        return ScanManager::Origin::Full;
    return ScanManager::Origin::Manual;
}
}

ScanRecord ScanRecord::fromSummary(const ScanSummary &summary, ScanManager::Origin origin)
{
    ScanRecord record;
    record.started = summary.started.isValid() ? summary.started : QDateTime::currentDateTime();
    record.elapsedMsecs = summary.elapsedMsecs;
    record.origin = origin;
    record.paths = summary.paths;
    record.scanned = summary.scanned;
    record.infected = summary.infected;
    record.errors = summary.errors;
    record.skipped = summary.skipped;
    record.cancelled = summary.cancelled;
    record.fatalError = summary.fatalError;
    record.threats = summary.threats;
    return record;
}

ScanSummary ScanRecord::toSummary() const
{
    ScanSummary summary;
    summary.paths = paths;
    summary.started = started;
    summary.elapsedMsecs = elapsedMsecs;
    summary.scanned = scanned;
    summary.infected = infected;
    summary.errors = errors;
    summary.skipped = skipped;
    summary.cancelled = cancelled;
    summary.fatalError = fatalError;
    summary.threats = threats;
    return summary;
}

QJsonObject ScanRecord::toJson() const
{
    QJsonArray threatList;
    for (const ScanResult &threat : threats)
        threatList.append(QJsonObject{{QStringLiteral("path"), threat.path}, {QStringLiteral("name"), threat.detail}});

    QJsonObject object{
        {QStringLiteral("started"), started.toString(Qt::ISODateWithMs)},
        {QStringLiteral("elapsedMsecs"), elapsedMsecs},
        {QStringLiteral("origin"), originKey(origin)},
        {QStringLiteral("paths"), QJsonArray::fromStringList(paths)},
        {QStringLiteral("scanned"), scanned},
        {QStringLiteral("infected"), infected},
        {QStringLiteral("errors"), errors},
        {QStringLiteral("skipped"), skipped},
        {QStringLiteral("cancelled"), cancelled},
        {QStringLiteral("threats"), threatList},
    };
    if (!fatalError.isEmpty())
        object.insert(QStringLiteral("fatalError"), fatalError);
    return object;
}

std::optional<ScanRecord> ScanRecord::fromJson(const QJsonObject &object)
{
    ScanRecord record;
    record.started = QDateTime::fromString(object.value(QStringLiteral("started")).toString(), Qt::ISODateWithMs);
    if (!record.started.isValid())
        return std::nullopt;
    record.elapsedMsecs = object.value(QStringLiteral("elapsedMsecs")).toInteger();
    record.origin = originFromKey(object.value(QStringLiteral("origin")).toString());
    for (const QJsonValue &path : object.value(QStringLiteral("paths")).toArray())
        record.paths << path.toString();
    record.scanned = object.value(QStringLiteral("scanned")).toInteger();
    record.infected = object.value(QStringLiteral("infected")).toInteger();
    record.errors = object.value(QStringLiteral("errors")).toInteger();
    record.skipped = object.value(QStringLiteral("skipped")).toInteger();
    record.cancelled = object.value(QStringLiteral("cancelled")).toBool();
    record.fatalError = object.value(QStringLiteral("fatalError")).toString();
    for (const QJsonValue &value : object.value(QStringLiteral("threats")).toArray()) {
        const QJsonObject threat = value.toObject();
        record.threats.append({threat.value(QStringLiteral("path")).toString(), ScanResult::Status::Infected,
                               threat.value(QStringLiteral("name")).toString()});
    }
    return record;
}

ScanHistory::ScanHistory(const QString &filePath, QObject *parent)
    : QObject(parent)
    , m_filePath(filePath)
{
    load();
}

QString ScanHistory::defaultFilePath()
{
    // GenericDataLocation : ~/.local/share (AppDataLocation ajouterait
    // l'organisation et l'application, toutes deux « linux-defender »).
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/linux-defender/history.json");
}

QList<ScanRecord> ScanHistory::records() const
{
    return m_records;
}

std::optional<ScanRecord> ScanHistory::last() const
{
    if (m_records.isEmpty())
        return std::nullopt;
    return m_records.first();
}

int ScanHistory::maxRecords() const
{
    return m_maxRecords;
}

void ScanHistory::setMaxRecords(int max)
{
    m_maxRecords = qMax(0, max);
    if (trim()) {
        save();
        emit changed();
    }
}

void ScanHistory::add(const ScanRecord &record)
{
    if (m_maxRecords == 0)
        return;
    m_records.prepend(record);
    trim();
    save();
    emit changed();
}

void ScanHistory::clear()
{
    if (m_records.isEmpty())
        return;
    m_records.clear();
    save();
    emit changed();
}

void ScanHistory::load()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly))
        return; // pas encore d'historique

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        qWarning() << "Historique illisible, ignoré :" << m_filePath << error.errorString();
        return;
    }
    for (const QJsonValue &value : document.object().value(QStringLiteral("scans")).toArray()) {
        if (const std::optional<ScanRecord> record = ScanRecord::fromJson(value.toObject()))
            m_records.append(*record);
    }
}

void ScanHistory::save()
{
    QJsonArray scans;
    for (const ScanRecord &record : std::as_const(m_records))
        scans.append(record.toJson());
    const QJsonObject root{{QStringLiteral("version"), kFormatVersion}, {QStringLiteral("scans"), scans}};

    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) < 0
        || !file.commit())
        qWarning() << "Enregistrement de l'historique impossible :" << m_filePath << file.errorString();
}

bool ScanHistory::trim()
{
    if (m_records.size() <= m_maxRecords)
        return false;
    m_records.resize(m_maxRecords);
    return true;
}
