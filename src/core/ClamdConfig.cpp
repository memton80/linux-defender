#include "ClamdConfig.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>

namespace
{
bool boolValue(const QString &value)
{
    const QString lower = value.toLower();
    return lower == QLatin1String("yes") || lower == QLatin1String("true") || lower == QLatin1String("1");
}
}

qint64 ClamdConfig::unscannedAbove() const
{
    return maxFileSize > 0 ? qMin(maxFileSize, kEngineMaxFileSize) : kEngineMaxFileSize;
}

QStringList ClamdConfig::defaultFiles()
{
    return {
        QStringLiteral("/etc/clamd.d/scan.conf"), // Fedora / RHEL (service clamd@scan)
        QStringLiteral("/etc/clamav/clamd.conf"), // Debian / Ubuntu / Arch
        QStringLiteral("/etc/clamd.conf"),        // openSUSE
    };
}

ClamdConfig ClamdConfig::parse(const QString &content)
{
    ClamdConfig config;
    static const QRegularExpression directive(QStringLiteral("^\\s*([A-Za-z]+)(?:\\s+(.*?))?\\s*$"));
    for (const QString &line : content.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch match = directive.match(line);
        if (!match.hasMatch())
            continue; // ligne vide ou commentée (« # » n'est pas une lettre)
        const QString name = match.captured(1);
        const QString value = match.captured(2);
        if (name == QLatin1String("Example")) {
            config.exampleLine = true;
        } else if (name == QLatin1String("LocalSocket")) {
            config.localSocket = value;
        } else if (name == QLatin1String("MaxFileSize")) {
            const qint64 size = parseSize(value);
            if (size >= 0)
                config.maxFileSize = size;
        } else if (name == QLatin1String("MaxScanSize")) {
            const qint64 size = parseSize(value);
            if (size >= 0)
                config.maxScanSize = size;
        } else if (name == QLatin1String("AlertExceedsMax")) {
            config.alertExceedsMax = boolValue(value);
        }
    }
    return config;
}

ClamdConfig ClamdConfig::read(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    ClamdConfig config = parse(QString::fromUtf8(file.readAll()));
    config.path = path;
    return config;
}

ClamdConfig ClamdConfig::forSocket(const QString &socketPath, const QStringList &files)
{
    ClamdConfig first;
    for (const QString &file : files) {
        const ClamdConfig config = read(file);
        if (config.path.isEmpty())
            continue;
        if (!socketPath.isEmpty() && QDir::cleanPath(config.localSocket) == QDir::cleanPath(socketPath))
            return config;
        if (first.path.isEmpty())
            first = config;
    }
    return first;
}

qint64 ClamdConfig::parseSize(const QString &value)
{
    static const QRegularExpression size(QStringLiteral("^(\\d+)([kKmMgG]?)$"));
    const QRegularExpressionMatch match = size.match(value.trimmed());
    if (!match.hasMatch())
        return -1;
    bool ok = false;
    qint64 bytes = match.captured(1).toLongLong(&ok);
    if (!ok)
        return -1;
    const QString unit = match.captured(2).toLower();
    if (unit == QLatin1String("k"))
        bytes *= 1024;
    else if (unit == QLatin1String("m"))
        bytes *= 1024 * 1024;
    else if (unit == QLatin1String("g"))
        bytes *= 1024 * 1024 * 1024;
    return bytes;
}
