#include "StatusDisplay.h"

#include <QCoreApplication>
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
    case ScanResult::Status::Error:
        break;
    }
    return svgIcon(QStringLiteral(":/icons/result-warning.svg"));
}

QString resultText(ScanResult::Status status)
{
    switch (status) {
    case ScanResult::Status::Clean:
        return tr("Sain");
    case ScanResult::Status::Infected:
        return tr("Infecté");
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
    if (summary.errors > 0)
        parts << plural(summary.errors, "%1 erreur", "%1 erreurs");
    const QString counts = parts.join(QStringLiteral(", "));

    if (!summary.fatalError.isEmpty())
        return tr("Scan interrompu (%1) :\n%2").arg(counts, summary.fatalError);
    if (summary.cancelled)
        return tr("Scan arrêté : %1.").arg(counts);
    return tr("Scan terminé : %1.").arg(counts);
}

QString pathsText(const QStringList &paths)
{
    if (paths.size() == 1)
        return paths.first();
    return plural(paths.size(), "%1 élément", "%1 éléments");
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

} // namespace StatusDisplay
