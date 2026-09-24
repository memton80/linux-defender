#include "Settings.h"

#include "ClamdClient.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace
{
const QString kSocketPathKey = QStringLiteral("clamd/socketPath");
const QString kCheckIntervalKey = QStringLiteral("clamd/checkInterval");
const QString kSignaturesMaxAgeKey = QStringLiteral("clamd/signaturesMaxAge");
const QString kQuickScanPathsKey = QStringLiteral("scan/quickScanPaths");
const QString kExcludedPathsKey = QStringLiteral("scan/excludedPaths");
const QString kScanHiddenKey = QStringLiteral("scan/scanHidden");
const QString kMaxFileSizeKey = QStringLiteral("scan/maxFileSizeMb");
const QString kUsbAutoScanKey = QStringLiteral("usb/autoScan");
const QString kUsbNotifyKey = QStringLiteral("usb/notify");
const QString kNotifyScanFinishedKey = QStringLiteral("notifications/scanFinished");
const QString kNotifyRealtimeKey = QStringLiteral("notifications/realtime");
const QString kNotifyClamdLostKey = QStringLiteral("notifications/clamdLost");
const QString kNotifySignaturesKey = QStringLiteral("notifications/signatures");
const QString kCloseToTrayKey = QStringLiteral("general/closeToTray");
const QString kHistoryMaxEntriesKey = QStringLiteral("history/maxEntries");

// Bornes des réglages numériques : un fichier de réglages modifié à la main
// ne doit pas produire de valeur absurde (vérification toutes les 0 s...).
int intValue(const QString &key, int defaultValue, int min, int max)
{
    bool ok = false;
    const int value = QSettings().value(key, defaultValue).toInt(&ok);
    return ok ? qBound(min, value, max) : defaultValue;
}

QStringList pathList(const QString &key)
{
    QStringList paths = QSettings().value(key).toStringList();
    paths.removeAll(QString());
    return paths;
}

void setPathList(const QString &key, const QStringList &paths)
{
    QSettings settings;
    if (paths.isEmpty())
        settings.remove(key);
    else
        settings.setValue(key, paths);
}
}

namespace Settings
{

QStringList Defaults::quickScanPaths()
{
    const QString home = QDir::homePath();
    QStringList paths;
    for (const auto location : {QStandardPaths::DownloadLocation, QStandardPaths::DesktopLocation,
                                QStandardPaths::DocumentsLocation}) {
        const QString path = QStandardPaths::writableLocation(location);
        // Sans dossiers XDG, Qt peut renvoyer le dossier personnel lui-même :
        // ce serait une analyse complète.
        if (!path.isEmpty() && QDir::cleanPath(path) != QDir::cleanPath(home) && QFileInfo(path).isDir()
            && !paths.contains(path))
            paths << path;
    }
    if (paths.isEmpty())
        paths << home;
    return paths;
}

QString socketPath()
{
    return QSettings().value(kSocketPathKey).toString();
}

void setSocketPath(const QString &path)
{
    QSettings settings;
    if (path.isEmpty())
        settings.remove(kSocketPathKey);
    else
        settings.setValue(kSocketPathKey, path);
}

QString effectiveSocketPath()
{
    const QString configured = socketPath();
    return configured.isEmpty() ? ClamdClient::detectSocketPath() : configured;
}

int checkInterval()
{
    return intValue(kCheckIntervalKey, Defaults::checkInterval, 5, 3600);
}

void setCheckInterval(int seconds)
{
    QSettings().setValue(kCheckIntervalKey, seconds);
}

int signaturesMaxAge()
{
    return intValue(kSignaturesMaxAgeKey, Defaults::signaturesMaxAge, 0, 365);
}

void setSignaturesMaxAge(int days)
{
    QSettings().setValue(kSignaturesMaxAgeKey, days);
}

QStringList quickScanPaths()
{
    const QStringList paths = pathList(kQuickScanPathsKey);
    return paths.isEmpty() ? Defaults::quickScanPaths() : paths;
}

void setQuickScanPaths(const QStringList &paths)
{
    setPathList(kQuickScanPathsKey, paths);
}

QStringList excludedPaths()
{
    return pathList(kExcludedPathsKey);
}

void setExcludedPaths(const QStringList &paths)
{
    setPathList(kExcludedPathsKey, paths);
}

bool scanHidden()
{
    return QSettings().value(kScanHiddenKey, Defaults::scanHidden).toBool();
}

void setScanHidden(bool enabled)
{
    QSettings().setValue(kScanHiddenKey, enabled);
}

int maxFileSizeMb()
{
    return intValue(kMaxFileSizeKey, Defaults::maxFileSizeMb, 0, 1024 * 1024);
}

void setMaxFileSizeMb(int megabytes)
{
    QSettings().setValue(kMaxFileSizeKey, megabytes);
}

ScanOptions scanOptions()
{
    ScanOptions options;
    options.excludedPaths = excludedPaths();
    options.scanHidden = scanHidden();
    options.maxFileSize = qint64(maxFileSizeMb()) * 1024 * 1024;
    return options;
}

bool usbAutoScan()
{
    return QSettings().value(kUsbAutoScanKey, Defaults::usbAutoScan).toBool();
}

void setUsbAutoScan(bool enabled)
{
    QSettings().setValue(kUsbAutoScanKey, enabled);
}

bool usbNotify()
{
    return QSettings().value(kUsbNotifyKey, Defaults::usbNotify).toBool();
}

void setUsbNotify(bool enabled)
{
    QSettings().setValue(kUsbNotifyKey, enabled);
}

bool notifyScanFinished()
{
    return QSettings().value(kNotifyScanFinishedKey, Defaults::notifyScanFinished).toBool();
}

void setNotifyScanFinished(bool enabled)
{
    QSettings().setValue(kNotifyScanFinishedKey, enabled);
}

bool notifyRealtime()
{
    return QSettings().value(kNotifyRealtimeKey, Defaults::notifyRealtime).toBool();
}

void setNotifyRealtime(bool enabled)
{
    QSettings().setValue(kNotifyRealtimeKey, enabled);
}

bool notifyClamdLost()
{
    return QSettings().value(kNotifyClamdLostKey, Defaults::notifyClamdLost).toBool();
}

void setNotifyClamdLost(bool enabled)
{
    QSettings().setValue(kNotifyClamdLostKey, enabled);
}

bool notifySignatures()
{
    return QSettings().value(kNotifySignaturesKey, Defaults::notifySignatures).toBool();
}

void setNotifySignatures(bool enabled)
{
    QSettings().setValue(kNotifySignaturesKey, enabled);
}

bool closeToTray()
{
    return QSettings().value(kCloseToTrayKey, Defaults::closeToTray).toBool();
}

void setCloseToTray(bool enabled)
{
    QSettings().setValue(kCloseToTrayKey, enabled);
}

int historyMaxEntries()
{
    return intValue(kHistoryMaxEntriesKey, Defaults::historyMaxEntries, 0, 10000);
}

void setHistoryMaxEntries(int count)
{
    QSettings().setValue(kHistoryMaxEntriesKey, count);
}

} // namespace Settings
