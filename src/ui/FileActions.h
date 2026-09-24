#pragma once

#include <QString>

class QPoint;
class QWidget;

// Actions sur un fichier d'une liste (résultats de scan, détections,
// historique). L'application ne modifie jamais les fichiers : elle aide
// seulement à les retrouver.
namespace FileActions
{
// Ouvre le gestionnaire de fichiers (Dolphin...) sur le dossier de `path`,
// avec le fichier sélectionné quand le gestionnaire le permet.
// `activationToken` : jeton xdg-activation (clic dans une notification), qui
// permet au gestionnaire de fichiers de prendre le focus sous Wayland.
void showInFileManager(const QString &path, const QString &activationToken = {});

// Menu contextuel d'une ligne : afficher dans le gestionnaire de fichiers,
// copier le chemin, copier `detail` (nom de la menace ou message d'erreur,
// si non vide ; `detailAction` est le libellé de cette action).
void execContextMenu(QWidget *parent, const QPoint &globalPos, const QString &path, const QString &detail = {},
                     const QString &detailAction = {});
}
