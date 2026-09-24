#pragma once

#include "core/ScanManager.h"

#include <QGroupBox>

class QCheckBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSortFilterProxyModel;
class QTreeView;
class ScanResultsModel;

/**
 * Section « Scan » de la fenêtre principale : boutons de scan, progression,
 * résumé et liste des fichiers analysés.
 *
 * Elle ne lance rien elle-même : tout passe par le ScanManager, qu'elle
 * observe aussi pour afficher les scans automatiques (clés USB).
 */
class ScanPanel : public QGroupBox
{
    Q_OBJECT

public:
    explicit ScanPanel(ScanManager *scans, QWidget *parent = nullptr);

    void chooseFiles();
    void chooseFolder();

private:
    void onScanStarted(const QStringList &paths, ScanManager::Origin origin);
    void onCounting(qint64 found);
    void onProgress(qint64 done, qint64 total);
    void onResults(const QList<ScanResult> &results);
    void onScanFinished(const ScanSummary &summary);
    void updateButtons();

    ScanManager *m_scans;
    ScanResultsModel *m_model;
    QSortFilterProxyModel *m_proxy;

    QPushButton *m_fileButton;
    QPushButton *m_folderButton;
    QPushButton *m_stopButton;
    QLabel *m_activity;
    QProgressBar *m_progress;
    QLabel *m_summaryIcon;
    QLabel *m_summary;
    QCheckBox *m_hideClean;
    QTreeView *m_view;
    QString m_currentPaths;
};
