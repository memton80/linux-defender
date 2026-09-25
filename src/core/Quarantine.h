#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>

#include <functional>

class QThread;

// Un fichier en quarantaine.
struct QuarantineEntry
{
    QString id;           // nom de ses fichiers dans le dossier de quarantaine
    QString originalPath; // emplacement d'origine
    QString threat;       // nom de la signature qui l'a fait détecter
    QDateTime date;       // mise en quarantaine
    qint64 size = 0;
    QString sha256;       // empreinte du fichier d'origine (hexadécimal)
    uint permissions = 0; // droits d'origine (0755...)
};

/**
 * Quarantaine : un fichier détecté est retiré de son emplacement et gardé,
 * inerte, dans ~/.local/share/linux-defender/quarantine (dossier 0700), d'où
 * il peut être restauré ou supprimé définitivement.
 *
 * Inerte : son contenu est brouillé (XOR avec une clé fixe, précédé d'un
 * en-tête). Il ne peut être ni exécuté ni ouvert par erreur, et ni clamd ni
 * la protection en temps réel ne le détectent de nouveau. Une empreinte
 * SHA-256 du fichier d'origine est gardée : la restauration la vérifie.
 *
 * Seuls les fichiers de l'utilisateur sont concernés : il faut pouvoir
 * supprimer l'original (dossier modifiable). L'application ne demande jamais
 * de privilèges pour cela.
 *
 * Les opérations lisent et écrivent des fichiers entiers : elles se font
 * dans un thread, une à la fois, et se terminent par finished().
 */
class Quarantine : public QObject
{
    Q_OBJECT

public:
    enum class Operation { Add, Restore, Remove };
    Q_ENUM(Operation)

    // Un fichier détecté à mettre en quarantaine.
    struct Item
    {
        QString path;
        QString threat;
    };

    explicit Quarantine(const QString &directory = defaultDirectory(), QObject *parent = nullptr);
    ~Quarantine() override; // attend la fin de l'opération en cours

    // ~/.local/share/linux-defender/quarantine
    static QString defaultDirectory();
    QString directory() const;

    // Fichiers en quarantaine, les plus récents en premier.
    QList<QuarantineEntry> entries() const;
    // Un fichier venu de cet emplacement est-il en quarantaine ?
    bool contains(const QString &originalPath) const;
    // Fichier mis en quarantaine depuis moins d'une minute ? La protection en
    // temps réel peut le signaler encore une fois, lu pour être déplacé.
    bool isRecentlyQuarantined(const QString &path) const;
    bool isBusy() const;

    // Met des fichiers en quarantaine ; finished() pour chacun.
    void add(const QList<Item> &items);
    // Remet un fichier à son emplacement d'origine (ou, si un fichier y est
    // déjà, à côté : « nom (restauré).ext »).
    void restore(const QString &id);
    // Supprime définitivement un fichier de la quarantaine.
    void remove(const QString &id);

    // Opérations bloquantes, exposées pour les tests. En cas d'échec, elles
    // ne laissent rien derrière elles et renvoient false avec `error`.
    static bool addFile(const QString &directory, const Item &item, QuarantineEntry *entry, QString *error);
    static bool restoreFile(const QString &directory, const QString &id, QString *restoredPath, QString *error);
    static bool removeFile(const QString &directory, const QString &id, QString *error);
    static QList<QuarantineEntry> readEntries(const QString &directory);

signals:
    // `path` : fichier d'origine (Add, Restore : emplacement final) ; `error` vide si réussi.
    void finished(Quarantine::Operation operation, const QString &path, const QString &error);
    void changed();
    // Plus aucune opération en cours ni en attente (après les finished()).
    void idle();

private:
    void enqueue(const std::function<void()> &job);
    void startNext();
    void reload();

    QString m_directory;
    QList<QuarantineEntry> m_entries;
    QSet<QString> m_originalPaths;
    QHash<QString, QDateTime> m_recent; // chemins d'origine mis en quarantaine, et quand
    QQueue<std::function<void()>> m_jobs;
    QThread *m_thread = nullptr;
};
