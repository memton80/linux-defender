#pragma once

#include "StatusDisplay.h"

#include <QFrame>
#include <QStackedWidget>

class QAbstractItemView;
class QLabel;
class QPushButton;
class QTreeWidget;

/**
 * Cadre aux coins arrondis qui regroupe une information : tuile du tableau de
 * bord, bandeau d'état, bilan d'un scan.
 *
 * Aucune feuille de style : il est dessiné avec les rôles de la palette,
 * relus à chaque affichage, et suit donc le thème (Breeze clair ou sombre,
 * même en cas de changement de thème). En mode Neutral, il est teinté par la
 * couleur du texte ; sinon par la couleur de son niveau (vert, orange,
 * rouge), comme les bandeaux de message des applications KDE.
 */
class Card : public QFrame
{
    Q_OBJECT

public:
    explicit Card(QWidget *parent = nullptr);

    StatusDisplay::Level level() const;
    void setLevel(StatusDisplay::Level level);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    StatusDisplay::Level m_level = StatusDisplay::Level::Neutral;
};

/**
 * Liste qui affiche un texte d'attente à sa place tant qu'elle est vide
 * (« Aucune détection »...), dans un cadre identique à celui de la liste.
 * Le modèle de la liste doit être défini avant la construction.
 */
class PlaceholderStack : public QStackedWidget
{
    Q_OBJECT

public:
    PlaceholderStack(QAbstractItemView *view, const QString &placeholder, QWidget *parent = nullptr);

    void setPlaceholderText(const QString &text);

private:
    void refresh();

    QAbstractItemView *m_view;
    QLabel *m_placeholder;
};

/**
 * Liste de dossiers (et de fichiers, selon le mode) modifiable : boutons
 * d'ajout avec le sélecteur de fichiers du système, bouton de retrait.
 */
class PathListEdit : public QWidget
{
    Q_OBJECT

public:
    enum class Mode { Folders, FoldersAndFiles };

    explicit PathListEdit(Mode mode, QWidget *parent = nullptr);

    QStringList paths() const;
    void setPaths(const QStringList &paths);

signals:
    // Liste modifiée par l'utilisateur (pas par setPaths()).
    void changed();

private:
    // Ajoute `path` s'il n'est pas déjà dans la liste ; renvoie true si ajouté.
    bool addPath(const QString &path);
    void updateButtons();

    QTreeWidget *m_list;
    QPushButton *m_removeButton;
};

namespace Widgets
{
// Police de `font` agrandie de `factor`, en gras si demandé.
QFont scaledFont(const QFont &font, qreal factor, bool bold = false);
// Titre en tête d'une page de la fenêtre ou des paramètres.
QLabel *pageTitle(const QString &text);
// Texte secondaire (légendes, explications) : couleur atténuée fournie par le
// thème (rôle PlaceholderText), qui suit les changements de thème.
void setSecondary(QWidget *widget);
// Affiche `icon` dans `label`, à la taille `size` (écrans HiDPI compris).
void setIcon(QLabel *label, const QIcon &icon, int size);
}
