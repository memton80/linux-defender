#pragma once

#include <QString>

#include <functional>

class QPoint;
class QWidget;

// Actions sur un fichier d'une liste (résultats de scan, détections,
// historique) : le retrouver, et le mettre en quarantaine (voir Quarantine),
// seule action qui le modifie, toujours à la demande de l'utilisateur.
namespace FileActions
{
// Ouvre le gestionnaire de fichiers (Dolphin...) sur le dossier de `path`,
// avec le fichier sélectionné quand le gestionnaire le permet.
// `activationToken` : jeton xdg-activation (clic dans une notification), qui
// permet au gestionnaire de fichiers de prendre le focus sous Wayland.
void showInFileManager(const QString &path, const QString &activationToken = {});

// Menu contextuel d'une ligne : mettre en quarantaine (si `quarantine` est
// fourni), afficher dans le gestionnaire de fichiers, copier le chemin,
// copier `detail` (nom de la menace ou message d'erreur, si non vide ;
// `detailAction` est le libellé de cette action).
void execContextMenu(QWidget *parent, const QPoint &globalPos, const QString &path, const QString &detail = {},
                     const QString &detailAction = {}, const std::function<void()> &quarantine = {});
}
