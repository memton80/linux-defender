#pragma once

#include <QByteArray>
#include <QDateTime>
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
    // Valeurs enregistrées telles quelles (filtres, compteurs) : ne pas les changer.
    enum class Status {
        Clean = 0,
        Infected = 1,   // signature d'un programme malveillant
        Error = 2,
        Suspicious = 3, // détection heuristique ou programme potentiellement indésirable
        Unscanned = 4,  // clamd n'a pas pu l'analyser (archive chiffrée, limite de taille)
    };
    static constexpr int kStatusCount = 5;

    QString path;
    Status status = Status::Clean;
    // Nom de la signature (Infected, Suspicious, Unscanned signalé par clamd)
    // ou explication (Error, Unscanned constaté par l'application).
    QString detail;
};

// Bilan d'un scan terminé.
struct ScanSummary
{
    // Menaces gardées dans `threats` : de quoi les citer (notification,
    // historique) sans garder en mémoire un nombre illimité de résultats.
    static constexpr int kMaxThreats = 100;

    QStringList paths;  // fichiers ou dossiers demandés
    QDateTime started;  // début du scan
    qint64 elapsedMsecs = 0;
    qint64 scanned = 0; // fichiers traités, quel que soit leur statut
    qint64 infected = 0;
    qint64 suspicious = 0;
    qint64 unscanned = 0; // transmis à clamd, mais pas (entièrement) analysés
    qint64 errors = 0;  // fichiers ou dossiers illisibles, erreurs de clamd
    qint64 skipped = 0; // fichiers ignorés car plus gros que ScanOptions::maxFileSize
    bool cancelled = false;
    QString fatalError; // non vide si le scan n'a pas pu aller au bout (clamd injoignable...)
    QList<ScanResult> threats;  // premières menaces trouvées (au plus kMaxThreats)
    QList<ScanResult> warnings; // premiers fichiers suspects ou non analysés (au plus kMaxThreats)
};

// Réglages d'un scan (voir les paramètres de l'application). Ils portent sur
// le contenu des dossiers parcourus : un fichier ou un dossier choisi
// explicitement par l'utilisateur est toujours analysé.
struct ScanOptions
{
    QStringList excludedPaths; // dossiers ou fichiers ignorés, avec tout leur contenu
    bool scanHidden = true;    // fichiers et dossiers cachés (nom commençant par un point)
    qint64 maxFileSize = 0;    // en octets ; 0 = pas de limite
    // Taille au-delà de laquelle clamd répond « OK » sans lire le fichier
    // (voir ClamdConfig::unscannedAbove()) ; 0 = inconnue. Un tel fichier est
    // signalé « non analysé », jamais « sain ».
    qint64 clamdUnscannedAbove = 0;

    bool operator==(const ScanOptions &other) const
    {
        return excludedPaths == other.excludedPaths && scanHidden == other.scanHidden
            && maxFileSize == other.maxFileSize && clamdUnscannedAbove == other.clamdUnscannedAbove;
    }
    bool operator!=(const ScanOptions &other) const { return !(*this == other); }
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
 * Les liens symboliques ne sont pas suivis ; /proc, /sys et /dev sont ignorés,
 * ainsi que ce qu'exclut ScanOptions.
 */
class ScanJob : public QObject
{
    Q_OBJECT

public:
    ScanJob(const QString &socketPath, const QStringList &paths, const ScanOptions &options = {},
            QObject *parent = nullptr);
    ~ScanJob() override; // annule le scan et attend la fin du thread

    QStringList paths() const;

    void start();
    // Demande l'arrêt : finished() arrive peu après, avec `cancelled` à true.
    void cancel();

    // Analyse une réponse de clamd à FILDES. Renvoie std::nullopt si elle
    // n'est pas reconnue (par exemple si clamd ne comprend pas la commande).
    // Une détection (« FOUND ») est classée menace, suspecte ou non analysée
    // selon le nom de sa signature (ThreatText::kind()).
    static std::optional<ScanResult> parseReply(const QString &path, const QByteArray &reply);
    // Explication d'un fichier « OK » plus gros que la limite de clamd.
    static QString unscannedSizeText(qint64 limit);

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
    // Parcourt `root`. Les fichiers ignorés à cause de leur taille sont
    // comptés dans `skipped` (s'il n'est pas nul), sans passer par `onFile`.
    bool walk(const QString &root, const FileVisitor &onFile, const ErrorVisitor &onError, qint64 *skipped = nullptr);
    ScanResult scanFile(const QString &path, QString *fatalError);
    Reply request(const QByteArray &command, int fileDescriptor, int timeoutMsecs);

    const QString m_socketPath;
    const QStringList m_paths;
    const ScanOptions m_options; // chemins d'exclusion déjà rendus canoniques
    std::atomic_bool m_cancelled{false};
    QThread *m_thread = nullptr;
};
