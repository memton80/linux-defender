#pragma once

#include <QString>

#include <functional>

class QDateTime;
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

// Le fichier a-t-il été modifié ou remplacé depuis `time` (sa détection) ?
// Ce n'est alors peut-être plus le fichier détecté : il faut l'analyser de
// nouveau avant de le mettre en quarantaine. Faux si `time` est inconnu.
bool changedSince(const QString &path, const QDateTime &time);

// Menu contextuel d'une ligne : mettre en quarantaine (si `quarantine` est
// fourni), analyser de nouveau (si `rescan` est fourni), afficher dans le
// gestionnaire de fichiers, copier le chemin, copier `detail` (nom de la
// menace ou message d'erreur, si non vide ; `detailAction` est le libellé de
// cette action).
void execContextMenu(QWidget *parent, const QPoint &globalPos, const QString &path, const QString &detail = {},
                     const QString &detailAction = {}, const std::function<void()> &quarantine = {},
                     const std::function<void()> &rescan = {});
}
