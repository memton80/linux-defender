#include "core/ClamdClient.h"
#include "core/ClamdWatcher.h"
#include "ui/MainWindow.h"
#include "ui/TrayIcon.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("linux-defender"));
    QApplication::setApplicationDisplayName(QStringLiteral("Linux Defender"));
    QApplication::setApplicationVersion(QStringLiteral(DEFENDER_VERSION));
    // Relie les fenêtres au fichier linux-defender.desktop (icône dans la barre des tâches sous Wayland).
    QApplication::setDesktopFileName(QStringLiteral("linux-defender"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/linux-defender.svg")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "Interface légère pour l'antivirus ClamAV."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption socketOption({QStringLiteral("s"), QStringLiteral("socket")},
                                          QCoreApplication::translate("main", "Chemin du socket clamd (détecté automatiquement par défaut)."),
                                          QCoreApplication::translate("main", "chemin"));
    const QCommandLineOption backgroundOption(QStringLiteral("background"),
                                              QCoreApplication::translate("main", "Démarre en arrière-plan, sans afficher la fenêtre."));
    parser.addOption(socketOption);
    parser.addOption(backgroundOption);
    parser.process(app);

    ClamdClient client;
    if (parser.isSet(socketOption))
        client.setSocketPath(parser.value(socketOption));

    ClamdWatcher watcher(&client);
    MainWindow window(&watcher);
    TrayIcon tray(&watcher);
    QObject::connect(&tray, &TrayIcon::showWindowRequested, &window, &MainWindow::showAndActivate);
    QObject::connect(&tray, &TrayIcon::toggleWindowRequested, &window, &MainWindow::toggleVisibility);

    // Avec une zone de notification, fermer la fenêtre la masque seulement et
    // l'application continue en arrière-plan. Sans zone de notification (rare
    // sous Plasma), il n'y aurait plus aucun moyen d'y revenir : fermer la
    // fenêtre quitte alors l'application, et elle est toujours affichée.
    const bool hasTray = QSystemTrayIcon::isSystemTrayAvailable();
    if (hasTray) {
        QApplication::setQuitOnLastWindowClosed(false);
        tray.show();
    }
    if (!hasTray || !parser.isSet(backgroundOption))
        window.show();

    watcher.start();
    return app.exec();
}
