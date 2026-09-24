#include "core/ClamdClient.h"
#include "core/ClamdWatcher.h"
#include "core/ScanHistory.h"
#include "core/ScanManager.h"
#include "core/Settings.h"
#include "system/OnAccessController.h"
#include "system/SingleInstance.h"
#include "system/UsbMonitor.h"
#include "ui/MainWindow.h"
#include "ui/TrayIcon.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QLibraryInfo>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("linux-defender")); // dossier des réglages
    QApplication::setApplicationName(QStringLiteral("linux-defender"));
    QApplication::setApplicationDisplayName(QStringLiteral("Linux Defender"));
    QApplication::setApplicationVersion(QStringLiteral(DEFENDER_VERSION));
    // Relie les fenêtres au fichier linux-defender.desktop (icône dans la barre des tâches sous Wayland).
    QApplication::setDesktopFileName(QStringLiteral("linux-defender"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/linux-defender.svg")));

    // Textes de Qt lui-même (boutons standard, sélecteur de fichiers...) dans
    // la langue du système, hors Plasma aussi. Sans traduction installée, ils
    // restent en anglais.
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslator);

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "Interface légère pour l'antivirus ClamAV."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption socketOption({QStringLiteral("s"), QStringLiteral("socket")},
                                          QCoreApplication::translate("main", "Chemin du socket clamd (sinon : réglages, puis détection automatique)."),
                                          QCoreApplication::translate("main", "chemin"));
    const QCommandLineOption backgroundOption(QStringLiteral("background"),
                                              QCoreApplication::translate("main", "Démarre en arrière-plan, sans afficher la fenêtre."));
    parser.addOption(socketOption);
    parser.addOption(backgroundOption);
    parser.process(app);

    // Déjà lancée ? On lui demande de réafficher sa fenêtre (sauf lancement en
    // arrière-plan, typiquement au démarrage de la session) et on quitte.
    const bool background = parser.isSet(backgroundOption);
    SingleInstance instance(QStringLiteral("linux-defender"));
    if (!instance.tryBecomePrimary(background ? QByteArray() : QByteArrayLiteral("show")))
        return 0;

    ClamdClient client;
    ClamdWatcher watcher(&client);
    ScanManager scans(&client);
    ScanHistory history;
    // Réglages pris en compte au lancement, puis à chaque modification.
    const QString socketFromCommandLine = parser.value(socketOption);
    const auto applySettings = [&] {
        client.setSocketPath(!socketFromCommandLine.isEmpty() ? socketFromCommandLine : Settings::effectiveSocketPath());
        watcher.setInterval(Settings::checkInterval() * 1000);
        scans.setOptions(Settings::scanOptions());
        history.setMaxRecords(Settings::historyMaxEntries());
    };
    applySettings();

    UsbMonitor usb;
    OnAccessController onAccess;
    MainWindow window(&watcher, &scans, &onAccess, &history);
    TrayIcon tray(&watcher, &scans, &onAccess);

    QObject::connect(&instance, &SingleInstance::messageReceived, &window, [&window](const QByteArray &message) {
        if (message == "show")
            window.showAndActivate();
    });
    QObject::connect(&tray, &TrayIcon::showWindowRequested, &window, &MainWindow::showAndActivate);
    QObject::connect(&tray, &TrayIcon::toggleWindowRequested, &window, &MainWindow::toggleVisibility);
    QObject::connect(&tray, &TrayIcon::quickScanRequested, &window, &MainWindow::startQuickScan);
    QObject::connect(&tray, &TrayIcon::scanFolderRequested, &window, &MainWindow::chooseFolderToScan);
    QObject::connect(&tray, &TrayIcon::showOnAccessRequested, &window, &MainWindow::showOnAccess);
    QObject::connect(&window, &MainWindow::windowActivated, &tray, &TrayIcon::acknowledgeThreats);
    // Résultats déjà sous les yeux de l'utilisateur : pas besoin de l'alerter via l'icône.
    QObject::connect(&scans, &ScanManager::scanFinished, &tray, [&window, &tray] {
        if (window.isActiveWindow())
            tray.acknowledgeThreats();
    });
    QObject::connect(&scans, &ScanManager::scanFinished, &history,
                     [&history](const ScanSummary &summary, ScanManager::Origin origin) {
                         history.add(ScanRecord::fromSummary(summary, origin));
                     });
    QObject::connect(&window, &MainWindow::settingsChanged, &watcher, [&applySettings, &watcher] {
        applySettings();
        watcher.checkNow();
    });
    QObject::connect(&usb, &UsbMonitor::removableMounted, &scans, [&scans](const QString &mountPoint) {
        if (Settings::usbAutoScan())
            scans.scan({mountPoint}, ScanManager::Origin::Usb);
    });

    // Avec une zone de notification, fermer la fenêtre la masque seulement et
    // l'application continue en arrière-plan. Sans zone de notification (rare
    // sous Plasma), il n'y aurait plus aucun moyen d'y revenir : fermer la
    // fenêtre quitte alors l'application, et elle est toujours affichée.
    const bool hasTray = QSystemTrayIcon::isSystemTrayAvailable();
    if (hasTray) {
        QApplication::setQuitOnLastWindowClosed(false);
        tray.show();
    }
    if (!hasTray || !background)
        window.show();

    watcher.start();
    // Protection en temps réel : état du service clamonacc, puis suivi de son journal.
    onAccess.refresh();
    onAccess.startMonitoring();
    return app.exec();
}
