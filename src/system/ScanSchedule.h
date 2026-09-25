#pragma once

#include "core/ClamdWatcher.h"
#include "core/ScanManager.h"

#include <QDBusConnection>
#include <QDateTime>
#include <QObject>
#include <QTimer>

/**
 * Analyses planifiées : rapide ou complète, chaque jour ou chaque semaine.
 *
 * Pas d'heure fixe ni de minuteur systemd : l'application tourne en
 * arrière-plan (démarrage automatique de session), et vérifie régulièrement
 * si une analyse est due. Une analyse manquée (ordinateur éteint, en veille)
 * est donc faite dès que possible, mais jamais pendant les premières minutes
 * de la session, qu'elle ralentirait.
 *
 * Une analyse due attend si une autre analyse est en cours, si clamd ne
 * répond pas, ou si l'ordinateur est sur batterie (UPower, réglable). Elle
 * compte comme faite quand elle se termine, même arrêtée par l'utilisateur
 * (elle ne recommence pas un quart d'heure plus tard) ; un échec (clamd
 * perdu) la laisse due.
 */
class ScanSchedule : public QObject
{
    Q_OBJECT

public:
    // Valeurs enregistrées dans les réglages : ne pas les changer.
    enum class Frequency { Never = 0, Daily = 1, Weekly = 2 };
    Q_ENUM(Frequency)
    enum class Kind { Quick = 0, Full = 1 };
    Q_ENUM(Kind)

    static constexpr int kStartDelayMsecs = 5 * 60 * 1000;     // après le lancement
    static constexpr int kCheckIntervalMsecs = 15 * 60 * 1000; // ensuite

    // `scans` et `watcher` ne sont pas possédés. `bus` : bus système (UPower).
    ScanSchedule(ScanManager *scans, ClamdWatcher *watcher, const QDBusConnection &bus = QDBusConnection::systemBus(),
                 QObject *parent = nullptr);

    void setSchedule(Frequency frequency, Kind kind, bool skipOnBattery);
    Frequency frequency() const;
    Kind kind() const;

    // Fin de la dernière analyse planifiée (enregistrée dans les réglages).
    QDateTime lastRun() const;
    void setLastRun(const QDateTime &time);
    // Prochaine analyse, au plus tôt (invalide si jamais) : l'heure réelle
    // dépend de l'ordinateur allumé, de clamd et de la batterie.
    QDateTime nextRun(const QDateTime &now = QDateTime::currentDateTime()) const;

    // Premières vérifications après kStartDelayMsecs.
    void start();
    // Vérifie tout de suite si une analyse est due, et la lance.
    void checkNow();

    // Une analyse est-elle due ? Chaque jour : 23 h au moins après la
    // précédente ; chaque semaine : 7 jours moins une heure (les
    // vérifications ont lieu tous les quarts d'heure, sans dériver). Jamais
    // faite : due tout de suite.
    static bool isDue(const QDateTime &lastRun, const QDateTime &now, Frequency frequency);
    static QDateTime nextRun(const QDateTime &lastRun, const QDateTime &now, Frequency frequency);

signals:
    // Analyse planifiée lancée (rapide ou complète).
    void scanLaunched(ScanSchedule::Kind kind);

private:
    void launch();
    void onScanFinished(const ScanSummary &summary, ScanManager::Origin origin);

    ScanManager *m_scans;
    ClamdWatcher *m_watcher;
    QDBusConnection m_bus;
    QTimer m_timer;
    Frequency m_frequency = Frequency::Never;
    Kind m_kind = Kind::Quick;
    bool m_skipOnBattery = true;
    bool m_checking = false; // lecture de l'état de la batterie en cours
};
