#pragma once

#include <QString>

// Réglages de l'application, enregistrés par QSettings dans
// ~/.config/linux-defender/linux-defender.conf.
namespace Settings
{
// Chemin du socket saisi par l'utilisateur ; vide = détection automatique.
QString socketPath();
void setSocketPath(const QString &path);

// Chemin réellement utilisé : celui des réglages, sinon celui détecté.
QString effectiveSocketPath();

// Scan automatique des clés USB au montage (activé par défaut).
bool usbAutoScan();
void setUsbAutoScan(bool enabled);
}
