#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QList>
#include <QObject>
#include <QString>

#include <optional>

// Une menace signalée par clamonacc.
struct OnAccessDetection
{
    // Moment où l'application a lu la détection. Le journal de clamonacc
    // n'est pas horodaté : invalide pour les détections antérieures au
    // lancement de l'application.
    QDateTime time;
    QString path;
    QString threat;
};

// Une ligne utile du journal de clamonacc.
struct OnAccessLogLine
{
    enum class Type { Detection, Error, RunStart };

    Type type = Type::Error;
    QString path;        // Detection
    QString threat;      // Detection
    QString message;     // Error : texte de l'erreur, sans « ERROR: »
    bool benign = false; // Error : sans conséquence pour la protection
};

/**
 * Suit le journal de clamonacc (option --log) pour connaître les détections
 * en temps réel.
 *
 * Aucune scrutation périodique : QFileSystemWatcher (inotify) signale chaque
 * ajout, et seules les nouvelles lignes sont lues. Le fichier peut ne pas
 * exister encore (service jamais lancé), être vidé ou remplacé (rotation) :
 * dans tous les cas la lecture reprend au bon endroit.
 *
 * Lignes reconnues (relevées sur clamonacc 1.5.4, avec --fdpass) :
 *   --------------------------------------      (début d'un lancement)
 *   /home/u/facture.pdf.exe: Win.Test.EICAR_HDB-1 FOUND
 *   ERROR: ClamInotif: could not watch path '/home', No space left on device
 *   Wait timeout exceeded; Could not connect to clamd   (erreur sans « ERROR: »)
 * clamonacc écrit souvent deux fois la même détection (écriture puis lecture
 * du fichier) : les doublons rapprochés sont ignorés.
 *
 * Certaines erreurs sont sans conséquence (OnAccessLogLine::benign) : un
 * dossier supprimé avant que clamonacc ait pu le surveiller, comme ceux que
 * les navigateurs créent et suppriment aussitôt dans leurs caches. Elles ne
 * sont pas signalées.
 */
class OnAccessLog : public QObject
{
    Q_OBJECT

public:
    explicit OnAccessLog(const QString &path, QObject *parent = nullptr);

    QString path() const;

    // Lit les détections déjà présentes (historyLoaded) et la dernière erreur
    // du dernier lancement (errorLogged), puis suit les ajouts.
    void start();

    static std::optional<OnAccessLogLine> parseLine(const QString &line);

signals:
    // Détections présentes dans le journal au démarrage (sans notification).
    void historyLoaded(const QList<OnAccessDetection> &detections);
    // Nouvelle détection, au moment où clamonacc l'écrit.
    void threatDetected(const OnAccessDetection &detection);
    // clamonacc vient de démarrer : les erreurs écrites avant ne le concernent plus.
    void runStarted();
    // Erreur du lancement en cours (clamd injoignable, privilèges...). Au
    // démarrage de la lecture : la dernière erreur déjà présente, s'il y en a.
    void errorLogged(const QString &message);

private:
    void rearm();
    void readNewData();
    bool isRecentDuplicate(const QString &path, const QString &threat);

    QString m_path;
    QFileSystemWatcher m_watcher;
    qint64 m_offset = 0;   // position de lecture dans le fichier
    quint64 m_inode = 0;   // change si le fichier est remplacé (rotation)
    QByteArray m_pending;  // fin de ligne pas encore écrite
    QByteArray m_tail;     // derniers octets lus : détecte un fichier vidé puis réécrit
    QString m_lastKey;     // dernière détection signalée, pour les doublons
    QDateTime m_lastTime;
};
