#pragma once

#include "core/ClamdWatcher.h"
#include "core/ScanJob.h"
#include "system/OnAccessController.h"

#include <QIcon>
#include <QString>

// Présentation (icônes et textes) partagée par l'icône de notification et la
// fenêtre principale, pour qu'elles affichent toujours la même chose.
namespace StatusDisplay
{
// État de clamd.
QIcon icon(ClamdWatcher::State state);
QString title(ClamdWatcher::State state);
// Version et signatures si clamd est connecté, message d'erreur sinon.
QString details(const ClamdWatcher &watcher);

// Scans.
QIcon scanningIcon();
QIcon threatIcon();
QIcon resultIcon(ScanResult::Status status);
QString resultText(ScanResult::Status status);
// « 1 234 fichiers analysés, 2 menaces détectées, 1 erreur. »
QString summaryText(const ScanSummary &summary);
// Chemins scannés, pour l'affichage : le chemin s'il est seul, sinon « 3 éléments ».
QString pathsText(const QStringList &paths);

// Protection en temps réel.
QIcon onAccessIcon(OnAccessController::State state);
QString onAccessTitle(OnAccessController::State state);
}
