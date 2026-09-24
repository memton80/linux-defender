#pragma once

#include "core/ScanManager.h"

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

class Card;
class PlaceholderStack;
class QButtonGroup;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QToolButton;
class QTreeView;
class ScanHistory;
class ScanResultsFilter;
class ScanResultsModel;

/**
 * Page « Analyse » de la fenêtre principale : lancement des analyses,
 * progression (fichier en cours, compteurs, durée), bilan et liste des
 * fichiers analysés, avec filtres, recherche et export.
 *
 * Elle ne lance rien elle-même : tout passe par le ScanManager, qu'elle
 * observe aussi pour afficher les scans automatiques (clés USB).
 */
class ScanPanel : public QWidget
{
    Q_OBJECT

public:
    ScanPanel(ScanManager *scans, ScanHistory *history, QWidget *parent = nullptr);

    void chooseFiles();
    void chooseFolder();
    void startQuickScan();
    void startFullScan();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void onScanStarted(const QStringList &paths, ScanManager::Origin origin);
    void onCounting(qint64 found);
    void onProgress(qint64 done, qint64 total);
    void onResults(const QList<ScanResult> &results);
    void onScanFinished(const ScanSummary &summary, ScanManager::Origin origin);
    // Bilan d'un scan terminé (le dernier, ou celui de l'historique au lancement).
    void showSummary(const ScanSummary &summary, ScanManager::Origin origin);
    void showIdle();
    void setStats(const QString &scanned, const QString &threats, const QString &errors, const QString &duration);
    void updateElapsed();
    void updateFilterButtons();
    // Texte affiché à la place de la liste vide : selon le filtre et l'activité.
    void updatePlaceholder();
    void updateButtons();
    void showContextMenu(const QPoint &position);
    void exportResults();

    ScanManager *m_scans;
    ScanHistory *m_history;
    ScanResultsModel *m_model;
    ScanResultsFilter *m_filter;

    QPushButton *m_quickButton;
    QPushButton *m_fullButton;
    QPushButton *m_folderButton;
    QPushButton *m_fileButton;
    QPushButton *m_stopButton;

    Card *m_activity;
    QLabel *m_activityIcon;
    QLabel *m_activityTitle;
    QLabel *m_activityDetail;
    QProgressBar *m_progress;
    QLabel *m_currentFile;

    QLabel *m_scannedValue;
    QLabel *m_threatsValue;
    QLabel *m_errorsValue;
    QLabel *m_durationValue;

    QButtonGroup *m_filterGroup;
    QList<QToolButton *> m_filterButtons; // tous, menaces, erreurs, sains
    QLineEdit *m_search;
    QPushButton *m_exportButton;
    QTreeView *m_view;
    PlaceholderStack *m_viewStack;
    QLabel *m_limitNote;

    // Scan en cours ou dernier scan affiché.
    bool m_hasSummary = false;
    ScanSummary m_summary;
    ScanManager::Origin m_origin = ScanManager::Origin::Manual;
    QString m_target;
    qint64 m_done = 0;
    qint64 m_counts[3] = {}; // résultats reçus, par statut (ScanResult::Status)
    QElapsedTimer m_elapsed;
    QTimer m_clock; // durée affichée pendant le scan
};
