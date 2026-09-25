#pragma once

#include "ScanManager.h"

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

// Une analyse terminée, telle que gardée dans l'historique.
struct ScanRecord
{
    QDateTime started;
    qint64 elapsedMsecs = 0;
    ScanManager::Origin origin = ScanManager::Origin::Manual;
    QStringList paths;
    bool systemAreas = false; // emplacements sensibles et programmes en cours analysés aussi
    qint64 scanned = 0;
    qint64 infected = 0;
    qint64 suspicious = 0;
    qint64 unscanned = 0;
    qint64 errors = 0;
    qint64 skipped = 0;
    bool cancelled = false;
    QString fatalError;
    QList<ScanResult> threats;  // au plus ScanSummary::kMaxThreats
    QList<ScanResult> warnings; // suspects et non analysés, au plus ScanSummary::kMaxThreats

    static ScanRecord fromSummary(const ScanSummary &summary, ScanManager::Origin origin);
    // Même bilan que le scan d'origine (pour les textes partagés avec les scans).
    ScanSummary toSummary() const;

    QJsonObject toJson() const;
    // std::nullopt si l'objet n'est pas une analyse lisible.
    static std::optional<ScanRecord> fromJson(const QJsonObject &object);
};

/**
 * Historique des analyses, du plus récent au plus ancien, enregistré en JSON
 * dans ~/.local/share/linux-defender/history.json.
 *
 * Le fichier est réécrit à chaque ajout (écriture atomique : un arrêt brutal
 * ne peut pas le corrompre). Un fichier illisible est ignoré : l'historique
 * repart de zéro plutôt que d'empêcher l'application de démarrer.
 */
class ScanHistory : public QObject
{
    Q_OBJECT

public:
    explicit ScanHistory(const QString &filePath = defaultFilePath(), QObject *parent = nullptr);

    static QString defaultFilePath();

    QList<ScanRecord> records() const; // la plus récente en premier
    std::optional<ScanRecord> last() const;

    // Nombre maximal d'analyses gardées (les plus anciennes sont oubliées) ;
    // 0 = pas d'historique : add() n'enregistre plus rien.
    int maxRecords() const;
    void setMaxRecords(int max);

    void add(const ScanRecord &record);
    void clear();

signals:
    void changed();

private:
    void load();
    void save();
    bool trim();

    QString m_filePath;
    QList<ScanRecord> m_records;
    int m_maxRecords = 100;
};
