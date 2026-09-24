#pragma once

#include "core/ClamdWatcher.h"

#include <QIcon>
#include <QString>

// Présentation de l'état de clamd (icône et textes), partagée par l'icône de
// notification et la fenêtre principale pour qu'elles affichent la même chose.
namespace StatusDisplay
{
QIcon icon(ClamdWatcher::State state);
QString title(ClamdWatcher::State state);
// Version et signatures si clamd est connecté, message d'erreur sinon.
QString details(const ClamdWatcher &watcher);
}
