#pragma once

#include <QByteArray>
#include <QString>

// Démarrage automatique à l'ouverture de session : un fichier .desktop dans
// ~/.config/autostart/, standard freedesktop respecté par Plasma (et GNOME...).
// L'application y est lancée avec --background : icône de notification seule.
namespace Autostart
{
// ~/.config/autostart/linux-defender.desktop
QString desktopFilePath();

bool isEnabled();
// Crée ou supprime le fichier. Renvoie false en cas d'échec, avec la raison dans `error`.
bool setEnabled(bool enabled, QString *error = nullptr);

// Contenu du fichier pour l'exécutable donné (exposé pour les tests).
QByteArray desktopFileContent(const QString &executable);
}
