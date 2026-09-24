#include "Autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

namespace
{
// Exécutable à inscrire dans le fichier : le simple nom de la commande si
// c'est celle installée dans le PATH (le fichier reste valable après une mise
// à jour), sinon le chemin complet (lancement depuis le dossier de build...).
QString currentExecutable()
{
    const QString self = QCoreApplication::applicationFilePath();
    const QString inPath = QStandardPaths::findExecutable(QFileInfo(self).fileName());
    if (!inPath.isEmpty() && QFileInfo(inPath).canonicalFilePath() == QFileInfo(self).canonicalFilePath())
        return QFileInfo(self).fileName();
    return self;
}

// Argument de la clé Exec, selon la spécification freedesktop des fichiers .desktop.
QString quoteExecArgument(QString argument)
{
    argument.replace(QLatin1Char('%'), QLatin1String("%%")); // % introduit les codes de champ
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9_/.+,:=@%-]+$"));
    if (safe.match(argument).hasMatch())
        return argument;

    // Entre guillemets, " ` $ et \ doivent être précédés d'une barre oblique inverse...
    QString quoted;
    for (const QChar c : std::as_const(argument)) {
        if (c == QLatin1Char('"') || c == QLatin1Char('`') || c == QLatin1Char('$') || c == QLatin1Char('\\'))
            quoted += QLatin1Char('\\');
        quoted += c;
    }
    // ... puis chaque barre oblique inverse est doublée, comme dans toute valeur d'un .desktop.
    quoted.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    return QLatin1Char('"') + quoted + QLatin1Char('"');
}
}

namespace Autostart
{

QString desktopFilePath()
{
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return QDir(config).filePath(QStringLiteral("autostart/linux-defender.desktop"));
}

bool isEnabled()
{
    return QFile::exists(desktopFilePath());
}

bool setEnabled(bool enabled, QString *error)
{
    const QString path = desktopFilePath();
    if (!enabled) {
        if (!QFile::exists(path) || QFile::remove(path))
            return true;
        if (error)
            *error = QCoreApplication::translate("Autostart", "Suppression impossible de %1").arg(path);
        return false;
    }

    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error)
            *error = QCoreApplication::translate("Autostart", "Création impossible du dossier %1").arg(QFileInfo(path).absolutePath());
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(desktopFileContent(currentExecutable())) < 0 || !file.commit()) {
        if (error)
            *error = QCoreApplication::translate("Autostart", "Écriture impossible de %1 : %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

QByteArray desktopFileContent(const QString &executable)
{
    const QString content = QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Linux Defender\n"
        "Comment=Lightweight interface for the ClamAV antivirus\n"
        "Comment[fr]=Interface légère pour l'antivirus ClamAV\n"
        "Exec=%1 --background\n"
        "Icon=linux-defender\n"
        "Terminal=false\n"
        "X-GNOME-Autostart-enabled=true\n")
        .arg(quoteExecArgument(executable));
    return content.toUtf8();
}

} // namespace Autostart
