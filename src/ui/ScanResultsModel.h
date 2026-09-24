#pragma once

#include "core/ScanJob.h"

#include <QAbstractTableModel>
#include <QFont>
#include <QIcon>
#include <QSortFilterProxyModel>

#include <optional>

/**
 * Résultats du scan en cours ou du dernier scan, pour la liste de la fenêtre.
 *
 * Mémoire bornée : un dossier personnel peut contenir des centaines de milliers
 * de fichiers sains. Seuls les kMaxCleanRows premiers sont listés ; les
 * suivants sont seulement comptés. Menaces et erreurs sont toujours listées.
 */
class ScanResultsModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column { StatusColumn, PathColumn, DetailColumn, ColumnCount };
    enum Role {
        StatusRole = Qt::UserRole, // ScanResult::Status, en entier (filtre)
        SortRole,                  // clé de tri : menaces, puis erreurs, puis fichiers sains
    };
    static constexpr int kMaxCleanRows = 10000;

    explicit ScanResultsModel(QObject *parent = nullptr);

    void clear();
    void append(const QList<ScanResult> &results);
    // Fichiers sains analysés mais non listés (au-delà de kMaxCleanRows).
    qint64 unlistedCleanCount() const;

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    QList<ScanResult> m_results;
    // Créés une seule fois : les recréer à chaque affichage de ligne referait le rendu des SVG.
    QIcon m_cleanIcon;
    QIcon m_infectedIcon;
    QIcon m_errorIcon;
    QFont m_infectedFont;
    int m_cleanRows = 0;
    qint64 m_unlistedClean = 0;
};

/**
 * Tri et filtre de la liste des résultats : un seul statut ou tous, et texte
 * recherché dans le chemin ou le nom de la menace (sans tenir compte de la casse).
 */
class ScanResultsFilter : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit ScanResultsFilter(QObject *parent = nullptr);

    // std::nullopt : tous les statuts.
    void setStatus(std::optional<ScanResult::Status> status);
    void setText(const QString &text);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    std::optional<ScanResult::Status> m_status;
    QString m_text;
};
