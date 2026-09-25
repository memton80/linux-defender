#pragma once

#include "OnAccessLog.h"

#include <QDBusConnection>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

#include <functional>

class QDBusMessage;

/**
 * Supervise la protection en temps réel assurée par clamonacc.
 *
 * clamonacc (fourni par ClamAV) surveille lui-même le système de fichiers avec
 * fanotify et transmet les fichiers à clamd. L'application ne réimplémente
 * rien de tout cela : elle observe le service systemd qui lance clamonacc et
 * lit son journal pour signaler les détections.
 *
 * Cette classe ne demande aucun privilège : elle lit l'état du service via
 * l'API D-Bus de systemd, en lecture seule, et le journal de clamonacc.
 */
class OnAccessController : public QObject
{
    Q_OBJECT

public:
    // Service systemd qui lance clamonacc pour Linux Defender, et fichiers
    // associés. Valeurs définies dans src/CMakeLists.txt, qui génère aussi les
    // fichiers installés : le code et les paquets ne peuvent pas diverger.
    static constexpr const char *kServiceName = DEFENDER_ONACCESS_SERVICE;
    static constexpr const char *kConfigPath = DEFENDER_ONACCESS_CONFIG;
    static constexpr const char *kLogPath = DEFENDER_ONACCESS_LOG;
    // Service fourni par certaines distributions (Debian, Ubuntu...). Il
    // déplace les fichiers infectés : il ne doit pas tourner en même temps.
    static constexpr const char *kDistributionServiceName = "clamav-clamonacc.service";

    enum class State {
        Unknown,        // vérification en cours, ou systemd injoignable
        NotInstalled,   // clamonacc absent du système
        ServiceMissing, // clamonacc présent, mais pas le service de Linux Defender
        Inactive,       // service installé mais arrêté
        Active,         // protection en marche
        Failed,         // le service a échoué
    };
    Q_ENUM(State)

    // `bus` et `logPath` : bus système et journal réel en temps normal ; les
    // tests utilisent un faux systemd sur un autre bus et un journal temporaire.
    explicit OnAccessController(const QDBusConnection &bus = QDBusConnection::systemBus(),
                                const QString &logPath = QString::fromLatin1(kLogPath),
                                QObject *parent = nullptr);

    State state() const;
    QString message() const;       // explication lisible de l'état
    QString clamonaccPath() const; // vide si clamonacc est absent
    QStringList watchedPaths() const; // dossiers surveillés (OnAccessIncludePath)
    // Service activé au démarrage de la machine (ou lancé) : état de la case
    // « Activer la protection en temps réel ».
    bool isEnabled() const;
    // Le dernier échec de clamonacc vient-il de la limite inotify du noyau ?
    bool inotifyLimitReached() const;

    // Dossiers où chercher clamonacc (par défaut : defaultSearchDirectories()).
    void setSearchDirectories(const QStringList &directories);

    // Relit l'état (asynchrone) ; stateChanged() suit.
    void refresh();
    // Démarre la lecture du journal : historyLoaded(), puis threatDetected().
    void startMonitoring();

    // Outils exposés pour les tests.
    static QStringList defaultSearchDirectories();
    static QString findClamonacc(const QStringList &directories = defaultSearchDirectories());
    // Distribution et celles dont elle dérive (ID et ID_LIKE de /etc/os-release).
    static QStringList distributionIds(const QString &osReleasePath = QStringLiteral("/etc/os-release"));
    // Commande d'installation de clamonacc selon la distribution (/etc/os-release).
    static QString installCommand(const QString &osReleasePath = QStringLiteral("/etc/os-release"));
    // Valeurs de OnAccessIncludePath dans la configuration de clamonacc.
    static QStringList readWatchedPaths(const QString &configPath = QString::fromLatin1(kConfigPath));
    // Explication lisible d'une erreur écrite par clamonacc dans son journal.
    static QString explainError(const QString &logError);
    static bool isInotifyLimitError(const QString &logError);
    // Chemin D-Bus d'une unité systemd (« a-b.service » -> « .../a_2db_2eservice »).
    static QString unitObjectPath(const QString &unitName);

signals:
    void stateChanged();
    void historyLoaded(const QList<OnAccessDetection> &detections);
    void threatDetected(const OnAccessDetection &detection);

private slots:
    void onUnitPropertiesChanged(const QDBusMessage &message);
    // systemd a relu ses fichiers (daemon-reload, par exemple après
    // l'installation du paquet) : le service a pu apparaître ou changer.
    void onManagerReloading(bool active);
    void onUnitFilesChanged();

private:
    void readUnit(const QString &unitName, const std::function<void(const QVariantMap &properties)> &onResult);
    void updateState();

    QDBusConnection m_bus;
    OnAccessLog m_log;
    QStringList m_searchDirectories = defaultSearchDirectories();
    QString m_clamonacc;
    QStringList m_watchedPaths;
    QVariantMap m_unit;            // propriétés systemd du service de Linux Defender
    bool m_distributionServiceActive = false;
    bool m_systemdReachable = true;
    QString m_lastError;           // dernière erreur du lancement en cours de clamonacc
    State m_state = State::Unknown;
    QString m_message;
};
