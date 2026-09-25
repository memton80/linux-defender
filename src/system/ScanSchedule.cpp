#include "ScanSchedule.h"

#include "core/Settings.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDir>

namespace
{
constexpr qint64 kHour = 3600;
constexpr qint64 kDailyInterval = 23 * kHour;
constexpr qint64 kWeeklyInterval = 7 * 24 * kHour - kHour;

qint64 interval(ScanSchedule::Frequency frequency)
{
    return frequency == ScanSchedule::Frequency::Weekly ? kWeeklyInterval : kDailyInterval;
}
}

ScanSchedule::ScanSchedule(ScanManager *scans, ClamdWatcher *watcher, const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_scans(scans)
    , m_watcher(watcher)
    , m_bus(bus)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        checkNow();
        m_timer.start(kCheckIntervalMsecs);
    });
    connect(m_scans, &ScanManager::scanFinished, this, &ScanSchedule::onScanFinished);
}

void ScanSchedule::setSchedule(Frequency frequency, Kind kind, bool skipOnBattery)
{
    m_frequency = frequency;
    m_kind = kind;
    m_skipOnBattery = skipOnBattery;
}

ScanSchedule::Frequency ScanSchedule::frequency() const
{
    return m_frequency;
}

ScanSchedule::Kind ScanSchedule::kind() const
{
    return m_kind;
}

QDateTime ScanSchedule::lastRun() const
{
    return Settings::scheduleLastRun();
}

void ScanSchedule::setLastRun(const QDateTime &time)
{
    Settings::setScheduleLastRun(time);
}

QDateTime ScanSchedule::nextRun(const QDateTime &now) const
{
    return nextRun(lastRun(), now, m_frequency);
}

void ScanSchedule::start()
{
    m_timer.start(kStartDelayMsecs);
}

bool ScanSchedule::isDue(const QDateTime &lastRun, const QDateTime &now, Frequency frequency)
{
    if (frequency == Frequency::Never)
        return false;
    // Jamais faite, ou horloge qui a reculé : due.
    if (!lastRun.isValid() || lastRun > now)
        return true;
    return lastRun.secsTo(now) >= interval(frequency);
}

QDateTime ScanSchedule::nextRun(const QDateTime &lastRun, const QDateTime &now, Frequency frequency)
{
    if (frequency == Frequency::Never)
        return {};
    if (isDue(lastRun, now, frequency))
        return now;
    return lastRun.addSecs(interval(frequency));
}

void ScanSchedule::checkNow()
{
    if (m_checking || !isDue(lastRun(), QDateTime::currentDateTime(), m_frequency))
        return;
    // Plus tard : une analyse (manuelle, clé USB) est en cours, ou clamd ne
    // répond pas (l'analyse échouerait aussitôt).
    if (m_scans->isScanning() || m_watcher->state() != ClamdWatcher::State::Connected)
        return;
    if (!m_skipOnBattery || !m_bus.isConnected()) {
        launch();
        return;
    }

    // Sur batterie ? Propriété OnBattery d'UPower. Sans UPower (ordinateur de
    // bureau, conteneur), l'analyse a lieu.
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.UPower"), QStringLiteral("/org/freedesktop/UPower"),
        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    call << QStringLiteral("org.freedesktop.UPower") << QStringLiteral("OnBattery");
    m_checking = true;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 5000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        m_checking = false;
        const QDBusPendingReply<QDBusVariant> reply = *watcher;
        const bool onBattery = !reply.isError() && reply.value().variant().toBool();
        if (!onBattery && !m_scans->isScanning())
            launch();
    });
}

void ScanSchedule::launch()
{
    if (m_kind == Kind::Full)
        m_scans->scan({QDir::homePath()}, ScanManager::Origin::Scheduled);
    else
        m_scans->quickScan(ScanManager::Origin::Scheduled);
    emit scanLaunched(m_kind);
}

void ScanSchedule::onScanFinished(const ScanSummary &summary, ScanManager::Origin origin)
{
    // Échec (clamd perdu en cours de route) : l'analyse reste due.
    if (origin == ScanManager::Origin::Scheduled && summary.fatalError.isEmpty())
        setLastRun(QDateTime::currentDateTime());
}
