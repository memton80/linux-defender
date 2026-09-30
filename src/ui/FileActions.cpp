#include "FileActions.h"

#include <QApplication>
#include <QClipboard>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileInfo>
#include <QIcon>
#include <QMenu>
#include <QUrl>

#include <functional>

namespace FileActions
{

void showInFileManager(const QString &path, const QString &activationToken)
{
    const QFileInfo info(path);
    const QUrl folder = QUrl::fromLocalFile(info.absolutePath());

    // Interface standard freedesktop (Dolphin, Nautilus...) : ouvre le dossier
    // et sélectionne le fichier. À défaut, simple ouverture du dossier.
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("/org/freedesktop/FileManager1"),
        QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("ShowItems"));
    call << QStringList{QUrl::fromLocalFile(info.absoluteFilePath()).toString()} << activationToken;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), qApp);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, qApp, [folder](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        if (watcher->isError())
            QDesktopServices::openUrl(folder);
    });
}

bool changedSince(const QString &path, const QDateTime &time)
{
    if (!time.isValid())
        return false;
    const QFileInfo info(path);
    // Date de modification du contenu, ou des métadonnées (fichier remplacé,
    // renommé à cet emplacement...).
    return info.lastModified() > time || info.metadataChangeTime() > time;
}

void execContextMenu(QWidget *parent, const QPoint &globalPos, const QString &path, const QString &detail,
                     const QString &detailAction, const std::function<void()> &quarantine,
                     const std::function<void()> &rescan)
{
    QMenu menu(parent);
    menu.setToolTipsVisible(true);
    const auto add = [&menu](const char *icon, const QString &text, const std::function<void()> &action) {
        QObject::connect(menu.addAction(QIcon::fromTheme(QString::fromLatin1(icon)), text), &QAction::triggered, action);
    };
    if (quarantine)
        add("folder-locked", QApplication::translate("FileActions", "Mettre en quarantaine"), quarantine);
    if (rescan) {
        QAction *action = menu.addAction(QIcon::fromTheme(QStringLiteral("system-search")),
                                         QApplication::translate("FileActions", "Analyser de nouveau"));
        action->setToolTip(QApplication::translate("FileActions", "Le fichier a été modifié depuis sa détection : "
                                                                  "il est analysé de nouveau avant toute mise en "
                                                                  "quarantaine."));
        QObject::connect(action, &QAction::triggered, rescan);
    }
    if (quarantine || rescan)
        menu.addSeparator();
    add("document-open-folder", QApplication::translate("FileActions", "Afficher dans le gestionnaire de fichiers"),
        [path] { showInFileManager(path); });
    menu.addSeparator();
    add("edit-copy", QApplication::translate("FileActions", "Copier le chemin"),
        [path] { QApplication::clipboard()->setText(path); });
    if (!detail.isEmpty())
        add("edit-copy", detailAction, [detail] { QApplication::clipboard()->setText(detail); });
    menu.exec(globalPos);
}

} // namespace FileActions
