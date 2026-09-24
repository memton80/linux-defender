#include "Settings.h"

#include "ClamdClient.h"

#include <QSettings>

namespace
{
const QString kSocketPathKey = QStringLiteral("clamd/socketPath");
const QString kUsbAutoScanKey = QStringLiteral("usb/autoScan");
}

namespace Settings
{

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

bool usbAutoScan()
{
    return QSettings().value(kUsbAutoScanKey, true).toBool();
}

void setUsbAutoScan(bool enabled)
{
    QSettings().setValue(kUsbAutoScanKey, enabled);
}

} // namespace Settings
