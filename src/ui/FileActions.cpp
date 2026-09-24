#include "FileActions.h"

#include <QApplication>
#include <QClipboard>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
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

void execContextMenu(QWidget *parent, const QPoint &globalPos, const QString &path, const QString &detail,
                     const QString &detailAction)
{
    QMenu menu(parent);
    const auto add = [&menu](const char *icon, const QString &text, const std::function<void()> &action) {
        QObject::connect(menu.addAction(QIcon::fromTheme(QString::fromLatin1(icon)), text), &QAction::triggered, action);
    };
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
