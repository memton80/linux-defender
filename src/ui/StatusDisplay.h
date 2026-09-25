#pragma once

#include "core/ClamdWatcher.h"
#include "core/ScanJob.h"
#include "core/ScanManager.h"
#include "system/OnAccessController.h"
#include "system/SystemDiagnostics.h"

#include <QColor>
#include <QDateTime>
#include <QIcon>
#include <QString>

// Présentation (icônes et textes) partagée par l'icône de notification et la
// fenêtre principale, pour qu'elles affichent toujours la même chose.
namespace StatusDisplay
{
// Gravité d'une information : couleur des bandeaux et icône associée.
enum class Level { Neutral, Positive, Warning, Negative };
// Couleurs de Breeze (celles des icônes) ; invalide pour Neutral (couleurs du thème).
QColor levelColor(Level level);
QIcon levelIcon(Level level);

// État de clamd.
QIcon icon(ClamdWatcher::State state);
QString title(ClamdWatcher::State state);
// Version et signatures si clamd est connecté, message d'erreur sinon.
QString details(const ClamdWatcher &watcher);
// Âge des signatures en jours, ou -1 si leur date est inconnue.
int signaturesAgeDays(const ClamdVersion &version);
// Signatures plus anciennes que `maxAgeDays` jours (0 = jamais obsolètes).
bool signaturesOutdated(const ClamdVersion &version, int maxAgeDays);

// Scans.
QIcon scanningIcon();
QIcon threatIcon();
QIcon resultIcon(ScanResult::Status status);
// Fichier mis en quarantaine (menace neutralisée).
QIcon quarantineIcon();
QString resultText(ScanResult::Status status);
// « 1 234 fichiers analysés, 2 menaces détectées, 1 erreur. »
QString summaryText(const ScanSummary &summary);
// Chemins scannés, pour l'affichage : le chemin s'il est seul, sinon « 3 éléments ».
QString pathsText(const QStringList &paths);
// « Analyse rapide », « Clé USB »...
QString originText(ScanManager::Origin origin);
// Cible d'un scan : chemins, ou noms des dossiers d'une analyse rapide.
QString targetText(ScanManager::Origin origin, const QStringList &paths);
// Cible d'une analyse rapide : noms des dossiers, et emplacements sensibles si demandés.
QString quickScanText(const QStringList &paths, bool systemAreas);
// Niveau et icône du bilan d'un scan (menaces, erreurs, sans problème).
Level summaryLevel(const ScanSummary &summary);

// Dates et durées : « il y a 5 minutes », « hier à 14:03 » ; « 3 min 12 s ».
QString relativeTime(const QDateTime &time);
QString durationText(qint64 msecs);

// Diagnostic : couleur et icône d'une vérification (Ok : vert, Info : neutre).
Level diagnosticLevel(DiagnosticItem::Level level);
QIcon diagnosticIcon(DiagnosticItem::Level level);

// Protection en temps réel.
QIcon onAccessIcon(OnAccessController::State state);
QString onAccessTitle(OnAccessController::State state);
Level onAccessLevel(OnAccessController::State state);
}
