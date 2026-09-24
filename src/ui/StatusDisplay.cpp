#include "StatusDisplay.h"

#include <QCoreApplication>
#include <QLocale>

namespace
{
// Même rendu en mode désactivé : la ligne d'état du menu de l'icône de
// notification est désactivée (non cliquable), mais doit garder sa couleur.
QIcon statusIcon(const QString &path)
{
    QIcon icon(path);
    icon.addFile(path, QSize(), QIcon::Disabled);
    return icon;
}
}

namespace StatusDisplay
{

QIcon icon(ClamdWatcher::State state)
{
    switch (state) {
    case ClamdWatcher::State::Connected:
        return statusIcon(QStringLiteral(":/icons/status-ok.svg"));
    case ClamdWatcher::State::Error:
        return statusIcon(QStringLiteral(":/icons/status-error.svg"));
    case ClamdWatcher::State::Unknown:
        break;
    }
    return statusIcon(QStringLiteral(":/icons/status-unknown.svg"));
}

QString title(ClamdWatcher::State state)
{
    switch (state) {
    case ClamdWatcher::State::Connected:
        return QCoreApplication::translate("StatusDisplay", "clamd connecté");
    case ClamdWatcher::State::Error:
        return QCoreApplication::translate("StatusDisplay", "clamd inaccessible");
    case ClamdWatcher::State::Unknown:
        break;
    }
    return QCoreApplication::translate("StatusDisplay", "Vérification de clamd…");
}

QString details(const ClamdWatcher &watcher)
{
    switch (watcher.state()) {
    case ClamdWatcher::State::Connected: {
        const ClamdVersion version = watcher.version();
        QString text = QCoreApplication::translate("StatusDisplay", "ClamAV %1").arg(version.engine);
        if (!version.signatures.isEmpty() && version.signaturesDate.isValid())
            text += QCoreApplication::translate("StatusDisplay", ", signatures n° %1 du %2")
                        .arg(version.signatures, QLocale().toString(version.signaturesDate, QLocale::ShortFormat));
        else if (!version.signatures.isEmpty())
            text += QCoreApplication::translate("StatusDisplay", ", signatures n° %1").arg(version.signatures);
        return text;
    }
    case ClamdWatcher::State::Error:
        return watcher.errorMessage();
    case ClamdWatcher::State::Unknown:
        break;
    }
    return {};
}

} // namespace StatusDisplay
