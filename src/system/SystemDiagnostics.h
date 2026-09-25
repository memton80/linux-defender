#pragma once

#include "OnAccessController.h"
#include "PrivilegedHelper.h"
#include "core/ClamdConfig.h"
#include "core/ClamdWatcher.h"

#include <QDBusConnection>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

// Une vérification du diagnostic, avec de quoi corriger un problème.
struct DiagnosticItem
{
    enum class Level { Ok, Info, Warning, Error };

    QString id; // « clamd », « signatures », « selinux », « limits », « onaccess », « inotify »
    Level level = Level::Ok;
    QString title;
    QString text;
    QString command; // commande(s) à lancer soi-même dans un terminal ; vide s'il n'y en a pas
    std::optional<PrivilegedHelper::Action> fix; // correction en un clic (programme d'aide)
    QString fixArgument;
    QString fixLabel; // texte du bouton de correction
};

// Tout ce que le diagnostic examine, relevé sur le système (voir SystemDiagnostics).
struct DiagnosticInput
{
    // clamd
    ClamdWatcher::State clamdState = ClamdWatcher::State::Unknown;
    ClamdClient::Error clamdError = ClamdClient::Error::NoError;
    QString clamdErrorMessage;
    ClamdVersion version;
    QString socketPath;
    ClamdConfig config;        // configuration du clamd utilisé (`path` vide : introuvable)
    QString clamdService;      // service systemd de clamd installé ; vide : aucun
    QString clamdServiceState; // son ActiveState : « active », « inactive », « failed »...
    bool freshclamInstalled = false;
    bool freshclamActive = false;
    int signaturesMaxAge = 3;  // jours ; 0 = jamais obsolètes
    QStringList distribution;  // ID et ID_LIKE de /etc/os-release

    // Accès au socket de clamd
    QString userName;
    QString socketGroup;
    bool userInSocketGroup = false;     // d'après la base des groupes (/etc/group)
    bool sessionHasSocketGroup = false; // groupes de la session en cours

    // SELinux
    bool selinuxEnforcing = false;
    std::optional<bool> antivirusCanScanSystem; // std::nullopt : booléen absent

    // Protection en temps réel
    OnAccessController::State onAccessState = OnAccessController::State::Unknown;
    QString onAccessMessage;
    bool inotifyLimitReached = false;
    qint64 inotifyMaxWatches = -1; // -1 : inconnue
};

/**
 * Diagnostic de l'installation : clamd, accès à son socket, signatures,
 * SELinux, limites d'analyse, protection en temps réel. Chaque problème est
 * expliqué, avec la commande exacte à lancer et, quand le programme d'aide
 * sait le faire, une correction en un clic.
 *
 * evaluate() est une fonction pure (testée à part) ; cette classe relève
 * l'état du système (fichiers, services systemd en lecture seule via D-Bus,
 * groupes de l'utilisateur) et la rappelle à chaque changement.
 */
class SystemDiagnostics : public QObject
{
    Q_OBJECT

public:
    // Au-dessous, la limite inotify est jugée basse pour surveiller /home.
    static constexpr qint64 kLowInotifyWatches = 131072;

    // `watcher` et `onAccess` ne sont pas possédés. `root` : racine des
    // fichiers lus (/proc, /sys) ; les tests en utilisent une autre.
    SystemDiagnostics(ClamdWatcher *watcher, OnAccessController *onAccess,
                      const QDBusConnection &bus = QDBusConnection::systemBus(), const QString &root = QStringLiteral("/"),
                      QObject *parent = nullptr);

    QList<DiagnosticItem> items() const;
    // Gravité la plus haute des vérifications (Ok si aucune).
    DiagnosticItem::Level worstLevel() const;
    // Vérification demandant une action (Warning ou Error) : la plus grave.
    std::optional<DiagnosticItem> mostSevere() const;

    // Relit l'état du système ; changed() suit (asynchrone).
    void refresh();

    static QList<DiagnosticItem> evaluate(const DiagnosticInput &input);

    // Services systemd de clamd selon les distributions, dans l'ordre de recherche.
    static QStringList clamdServices();
    static QString clamdInstallCommand(const QStringList &distribution);
    static QString freshclamInstallCommand(const QStringList &distribution);
    // SELinux appliqué, et booléen antivirus_can_scan_system (absent : std::nullopt).
    static void readSelinux(const QString &root, bool *enforcing, std::optional<bool> *antivirusCanScanSystem);
    static qint64 readInotifyMaxWatches(const QString &root);

signals:
    void changed();

private:
    void readService(const QString &name);
    void finishRefresh();

    ClamdWatcher *m_watcher;
    OnAccessController *m_onAccess;
    QDBusConnection m_bus;
    QString m_root;
    DiagnosticInput m_input;
    QHash<QString, QVariantMap> m_services; // propriétés systemd lues, par service
    int m_pending = 0;
    bool m_refreshAgain = false;
    QList<DiagnosticItem> m_items;
};
