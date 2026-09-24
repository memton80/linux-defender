#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <optional>

class QThread;

// Résultat du scan d'un fichier.
struct ScanResult
{
    enum class Status {
        Clean = 0,
        Infected = 1,
        Error = 2,
    };

    QString path;
    Status status = Status::Clean;
    QString detail; // nom de la menace (Infected) ou message d'erreur (Error)
};

// Bilan d'un scan terminé.
struct ScanSummary
{
    QStringList paths;  // fichiers ou dossiers demandés
    qint64 scanned = 0; // fichiers traités : sains + infectés + en erreur
    qint64 infected = 0;
    qint64 errors = 0;  // fichiers ou dossiers illisibles, erreurs de clamd
    bool cancelled = false;
    QString fatalError; // non vide si le scan n'a pas pu aller au bout (clamd injoignable...)
};

/**
 * Scan d'un ou plusieurs fichiers ou dossiers par clamd, avec la commande FILDES.
 *
 * FILDES : l'application ouvre elle-même chaque fichier, avec les droits de
 * l'utilisateur, puis transmet le descripteur ouvert à clamd par le socket
 * Unix. clamd peut ainsi analyser des fichiers qu'il n'aurait pas le droit
 * d'ouvrir lui-même : dossier personnel, clés USB montées sous
 * /run/media/<utilisateur>/... C'est ce que fait `clamdscan --fdpass`.
 *
 * Tout le travail se fait dans un thread dédié : même sur un très gros dossier
 * ou une clé USB lente, l'interface ne se fige jamais. Les signaux sont émis
 * depuis ce thread ; Qt les transmet au thread de l'interface via sa file
 * d'événements (connexion automatiquement « en file d'attente »).
 *
 * Déroulement :
 *   1. PING : clamd répond-il ? Sinon, inutile de parcourir le dossier.
 *   2. Comptage des fichiers, pour afficher une vraie progression.
 *   3. Scan des fichiers, un par un.
 * Les liens symboliques ne sont pas suivis ; /proc, /sys et /dev sont ignorés.
 */
class ScanJob : public QObject
{
    Q_OBJECT

public:
    ScanJob(const QString &socketPath, const QStringList &paths, QObject *parent = nullptr);
    ~ScanJob() override; // annule le scan et attend la fin du thread

    QStringList paths() const;

    void start();
    // Demande l'arrêt : finished() arrive peu après, avec `cancelled` à true.
    void cancel();

    // Analyse une réponse de clamd à FILDES. Renvoie std::nullopt si elle
    // n'est pas reconnue (par exemple si clamd ne comprend pas la commande).
    static std::optional<ScanResult> parseReply(const QString &path, const QByteArray &reply);

signals:
    // Étape 2 : nombre de fichiers trouvés jusqu'ici.
    void counting(qint64 found);
    // Étape 3 : `done` fichiers traités sur `total`.
    void progressChanged(qint64 done, qint64 total);
    // Résultats envoyés par lots, pour ne pas inonder l'interface de signaux.
    void resultsReady(const QList<ScanResult> &results);
    void finished(const ScanSummary &summary);

private:
    struct Reply
    {
        QByteArray data;    // réponse de clamd, sans l'octet nul final
        QString error;      // vide si tout s'est bien passé
        bool fatal = false; // clamd injoignable : inutile de continuer
    };

    using FileVisitor = std::function<bool(const QString &path)>;
    using ErrorVisitor = std::function<bool(const QString &path, const QString &error)>;

    // Les méthodes suivantes s'exécutent dans le thread de travail et sont
    // bloquantes : ne jamais les appeler depuis le thread de l'interface.
    void run();
    bool walk(const QString &root, const FileVisitor &onFile, const ErrorVisitor &onError);
    ScanResult scanFile(const QString &path, QString *fatalError);
    Reply request(const QByteArray &command, int fileDescriptor, int timeoutMsecs);

    const QString m_socketPath;
    const QStringList m_paths;
    std::atomic_bool m_cancelled{false};
    QThread *m_thread = nullptr;
};
