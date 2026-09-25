#include "StatusDisplay.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QLocale>

namespace
{
QString tr(const char *text)
{
    return QCoreApplication::translate("StatusDisplay", text);
}

// Même rendu en mode désactivé : la ligne d'état du menu de l'icône de
// notification est désactivée (non cliquable), mais doit garder sa couleur.
QIcon svgIcon(const QString &path)
{
    QIcon icon(path);
    icon.addFile(path, QSize(), QIcon::Disabled);
    return icon;
}

QString number(qint64 value)
{
    return QLocale().toString(value);
}

// En français, 0 et 1 sont au singulier : « 0 fichier analysé », « 2 fichiers analysés ».
QString plural(qint64 count, const char *singular, const char *pluralForm)
{
    return tr(count > 1 ? pluralForm : singular).arg(number(count));
}
}

namespace StatusDisplay
{

QColor levelColor(Level level)
{
    switch (level) {
    case Level::Positive:
        return QColor(0x27, 0xae, 0x60);
    case Level::Warning:
        return QColor(0xf6, 0x74, 0x00);
    case Level::Negative:
        return QColor(0xda, 0x44, 0x53);
    case Level::Neutral:
        break;
    }
    return {};
}

QIcon levelIcon(Level level)
{
    switch (level) {
    case Level::Positive:
        return svgIcon(QStringLiteral(":/icons/status-ok.svg"));
    case Level::Warning:
        return svgIcon(QStringLiteral(":/icons/result-warning.svg"));
    case Level::Negative:
        return svgIcon(QStringLiteral(":/icons/status-error.svg"));
    case Level::Neutral:
        break;
    }
    return svgIcon(QStringLiteral(":/icons/status-unknown.svg"));
}

QIcon icon(ClamdWatcher::State state)
{
    switch (state) {
    case ClamdWatcher::State::Connected:
        return svgIcon(QStringLiteral(":/icons/status-ok.svg"));
    case ClamdWatcher::State::Error:
        return svgIcon(QStringLiteral(":/icons/status-error.svg"));
    case ClamdWatcher::State::Unknown:
        break;
    }
    return svgIcon(QStringLiteral(":/icons/status-unknown.svg"));
}

QString title(ClamdWatcher::State state)
{
    switch (state) {
    case ClamdWatcher::State::Connected:
        return tr("clamd connecté");
    case ClamdWatcher::State::Error:
        return tr("clamd inaccessible");
    case ClamdWatcher::State::Unknown:
        break;
    }
    return tr("Vérification de clamd…");
}

QString details(const ClamdWatcher &watcher)
{
    switch (watcher.state()) {
    case ClamdWatcher::State::Connected: {
        const ClamdVersion version = watcher.version();
        QString text = tr("ClamAV %1").arg(version.engine);
        if (!version.signatures.isEmpty() && version.signaturesDate.isValid())
            text += tr(", signatures n° %1 du %2")
                        .arg(version.signatures, QLocale().toString(version.signaturesDate, QLocale::ShortFormat));
        else if (!version.signatures.isEmpty())
            text += tr(", signatures n° %1").arg(version.signatures);
        return text;
    }
    case ClamdWatcher::State::Error:
        return watcher.errorMessage();
    case ClamdWatcher::State::Unknown:
        break;
    }
    return {};
}

int signaturesAgeDays(const ClamdVersion &version)
{
    if (!version.signaturesDate.isValid())
        return -1;
    return int(qMax<qint64>(0, version.signaturesDate.secsTo(QDateTime::currentDateTime()) / 86400));
}

bool signaturesOutdated(const ClamdVersion &version, int maxAgeDays)
{
    return maxAgeDays > 0 && signaturesAgeDays(version) >= maxAgeDays;
}

QIcon scanningIcon()
{
    return svgIcon(QStringLiteral(":/icons/status-scanning.svg"));
}

QIcon threatIcon()
{
    return svgIcon(QStringLiteral(":/icons/result-threat.svg"));
}

QIcon resultIcon(ScanResult::Status status)
{
    switch (status) {
    case ScanResult::Status::Clean:
        return svgIcon(QStringLiteral(":/icons/status-ok.svg"));
    case ScanResult::Status::Infected:
        return threatIcon();
    case ScanResult::Status::Suspicious:
        return svgIcon(QStringLiteral(":/icons/result-suspicious.svg"));
    case ScanResult::Status::Unscanned:
        return svgIcon(QStringLiteral(":/icons/result-unscanned.svg"));
    case ScanResult::Status::Error:
        break;
    }
    return svgIcon(QStringLiteral(":/icons/result-warning.svg"));
}

QIcon quarantineIcon()
{
    return QIcon::fromTheme(QStringLiteral("folder-locked"), svgIcon(QStringLiteral(":/icons/status-ok.svg")));
}

QString resultText(ScanResult::Status status)
{
    switch (status) {
    case ScanResult::Status::Clean:
        return tr("Sain");
    case ScanResult::Status::Infected:
        return tr("Infecté");
    case ScanResult::Status::Suspicious:
        return tr("Suspect");
    case ScanResult::Status::Unscanned:
        return tr("Non analysé");
    case ScanResult::Status::Error:
        break;
    }
    return tr("Erreur");
}

QString summaryText(const ScanSummary &summary)
{
    QStringList parts;
    parts << plural(summary.scanned, "%1 fichier analysé", "%1 fichiers analysés");
    parts << (summary.infected == 0 ? tr("aucune menace")
                                    : plural(summary.infected, "%1 menace détectée", "%1 menaces détectées"));
    if (summary.suspicious > 0)
        parts << plural(summary.suspicious, "%1 fichier suspect", "%1 fichiers suspects");
    if (summary.unscanned > 0)
        parts << plural(summary.unscanned, "%1 fichier non analysé par clamd", "%1 fichiers non analysés par clamd");
    if (summary.errors > 0)
        parts << plural(summary.errors, "%1 erreur", "%1 erreurs");
    if (summary.skipped > 0)
        parts << plural(summary.skipped, "%1 fichier ignoré (taille)", "%1 fichiers ignorés (taille)");
    const QString counts = parts.join(QStringLiteral(", "));

    if (!summary.fatalError.isEmpty())
        return tr("Analyse interrompue (%1) :\n%2").arg(counts, summary.fatalError);
    if (summary.cancelled)
        return tr("Analyse arrêtée : %1.").arg(counts);
    return tr("Analyse terminée : %1.").arg(counts);
}

QString pathsText(const QStringList &paths)
{
    if (paths.size() == 1)
        return paths.first();
    return plural(paths.size(), "%1 élément", "%1 éléments");
}

QString originText(ScanManager::Origin origin)
{
    switch (origin) {
    case ScanManager::Origin::Quick:
        return tr("Analyse rapide");
    case ScanManager::Origin::Full:
        return tr("Analyse complète");
    case ScanManager::Origin::Usb:
        return tr("Clé USB");
    case ScanManager::Origin::Scheduled:
        return tr("Analyse planifiée");
    case ScanManager::Origin::Manual:
        break;
    }
    return tr("Analyse personnalisée");
}

QString targetText(ScanManager::Origin origin, const QStringList &paths)
{
    // Analyse rapide : les noms des dossiers parlent plus que leurs chemins.
    const bool quick = origin == ScanManager::Origin::Quick || origin == ScanManager::Origin::Scheduled;
    if (quick && paths.size() > 1 && paths.size() <= 4) {
        QStringList names;
        for (const QString &path : paths)
            names << QFileInfo(path).fileName();
        return names.join(QStringLiteral(", "));
    }
    return pathsText(paths);
}

QString quickScanText(const QStringList &paths, bool systemAreas)
{
    const QString folders = targetText(ScanManager::Origin::Quick, paths);
    return systemAreas ? tr("%1, démarrage automatique, fichiers temporaires et programmes en cours").arg(folders)
                       : folders;
}

Level summaryLevel(const ScanSummary &summary)
{
    if (summary.infected > 0)
        return Level::Negative;
    // Les fichiers non analysés (archives chiffrées, fichiers trop gros) sont
    // cités dans le bilan sans le colorer : un dossier de téléchargements en
    // contient souvent, et un bandeau toujours orange ne signalerait plus rien.
    if (!summary.fatalError.isEmpty() || summary.errors > 0 || summary.suspicious > 0 || summary.cancelled)
        return Level::Warning;
    return Level::Positive;
}

QString relativeTime(const QDateTime &time)
{
    if (!time.isValid())
        return {};
    const QDateTime now = QDateTime::currentDateTime();
    const qint64 seconds = time.secsTo(now);
    const QString clock = QLocale().toString(time.time(), QLocale::ShortFormat);
    if (seconds >= 0 && seconds < 60)
        return tr("à l'instant");
    if (seconds >= 0 && seconds < 3600)
        return plural(seconds / 60, "il y a %1 minute", "il y a %1 minutes");
    const qint64 days = time.date().daysTo(now.date());
    if (days == 0)
        return tr("aujourd'hui à %1").arg(clock);
    if (days == 1)
        return tr("hier à %1").arg(clock);
    return tr("le %1 à %2").arg(QLocale().toString(time.date(), QLocale::ShortFormat), clock);
}

QString durationText(qint64 msecs)
{
    const qint64 seconds = msecs / 1000;
    if (seconds < 60)
        return tr("%1 s").arg(seconds);
    if (seconds < 3600)
        return tr("%1 min %2 s").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
    return tr("%1 h %2 min").arg(seconds / 3600).arg((seconds % 3600) / 60, 2, 10, QLatin1Char('0'));
}

Level diagnosticLevel(DiagnosticItem::Level level)
{
    switch (level) {
    case DiagnosticItem::Level::Ok:
        return Level::Positive;
    case DiagnosticItem::Level::Warning:
        return Level::Warning;
    case DiagnosticItem::Level::Error:
        return Level::Negative;
    case DiagnosticItem::Level::Info:
        break;
    }
    return Level::Neutral;
}

QIcon diagnosticIcon(DiagnosticItem::Level level)
{
    if (level == DiagnosticItem::Level::Info)
        return QIcon::fromTheme(QStringLiteral("dialog-information"), levelIcon(Level::Neutral));
    return levelIcon(diagnosticLevel(level));
}

QIcon onAccessIcon(OnAccessController::State state)
{
    switch (state) {
    case OnAccessController::State::Active:
        return svgIcon(QStringLiteral(":/icons/status-ok.svg"));
    case OnAccessController::State::Failed:
        return svgIcon(QStringLiteral(":/icons/status-error.svg"));
    case OnAccessController::State::NotInstalled:
    case OnAccessController::State::ServiceMissing:
        return svgIcon(QStringLiteral(":/icons/result-warning.svg"));
    case OnAccessController::State::Inactive:
    case OnAccessController::State::Unknown:
        break;
    }
    return svgIcon(QStringLiteral(":/icons/status-unknown.svg"));
}

QString onAccessTitle(OnAccessController::State state)
{
    switch (state) {
    case OnAccessController::State::Active:
        return tr("Protection en temps réel active");
    case OnAccessController::State::Inactive:
        return tr("Protection en temps réel désactivée");
    case OnAccessController::State::Failed:
        return tr("Protection en temps réel en erreur");
    case OnAccessController::State::NotInstalled:
        return tr("clamonacc non installé");
    case OnAccessController::State::ServiceMissing:
        return tr("Service de protection non installé");
    case OnAccessController::State::Unknown:
        break;
    }
    return tr("Protection en temps réel : état inconnu");
}

Level onAccessLevel(OnAccessController::State state)
{
    switch (state) {
    case OnAccessController::State::Active:
        return Level::Positive;
    case OnAccessController::State::Failed:
        return Level::Negative;
    case OnAccessController::State::NotInstalled:
    case OnAccessController::State::ServiceMissing:
        return Level::Warning;
    case OnAccessController::State::Inactive:
    case OnAccessController::State::Unknown:
        break;
    }
    return Level::Neutral;
}

} // namespace StatusDisplay
