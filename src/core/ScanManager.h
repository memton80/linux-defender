#pragma once

#include "ScanJob.h"

#include <QObject>

class ClamdClient;

/**
 * Point d'entrée unique pour lancer des scans : scan à la demande (fenêtre,
 * icône de notification) ou automatique (clé USB branchée).
 *
 * Un seul scan tourne à la fois. Une demande arrivée pendant un scan (une
 * deuxième clé USB, par exemple) est mise en file d'attente et démarre ensuite.
 */
class ScanManager : public QObject
{
    Q_OBJECT

public:
    enum class Origin {
        Manual, // fichiers ou dossier choisis par l'utilisateur
        Usb,    // lancé automatiquement au branchement d'une clé USB
        Quick,  // analyse rapide : dossiers des paramètres (Téléchargements...)
        Full,   // analyse complète : dossier personnel
    };
    Q_ENUM(Origin)

    // `client` n'est pas possédé : il fournit le chemin du socket de clamd.
    explicit ScanManager(ClamdClient *client, QObject *parent = nullptr);

    // Options des scans suivants (le scan en cours garde les siennes).
    void setOptions(const ScanOptions &options);
    ScanOptions options() const;

    // Lance un scan, ou le met en file d'attente si un scan est déjà en cours.
    void scan(const QStringList &paths, Origin origin);
    // Arrête le scan en cours et vide la file d'attente.
    void cancelAll();

    bool isScanning() const;
    QStringList currentPaths() const;
    Origin currentOrigin() const;

signals:
    void scanStarted(const QStringList &paths, ScanManager::Origin origin);
    void counting(qint64 found);
    void progressChanged(qint64 done, qint64 total);
    void resultsReady(const QList<ScanResult> &results);
    void scanFinished(const ScanSummary &summary, ScanManager::Origin origin);

private:
    struct Request
    {
        QStringList paths;
        Origin origin;
    };

    void startNext();

    ClamdClient *m_client;
    ScanOptions m_options;
    QList<Request> m_queue;
    ScanJob *m_job = nullptr;
    Origin m_origin = Origin::Manual;
};
