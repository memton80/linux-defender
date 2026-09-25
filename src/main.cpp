#include "core/ClamdClient.h"
#include "core/ClamdWatcher.h"
#include "core/ScanHistory.h"
#include "core/ScanManager.h"
#include "core/Settings.h"
#include "system/OnAccessController.h"
#include "system/PrivilegedHelper.h"
#include "system/SingleInstance.h"
#include "system/SystemDiagnostics.h"
#include "system/UsbMonitor.h"
#include "ui/MainWindow.h"
#include "ui/TrayIcon.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QIcon>
#include <QLibraryInfo>
#include <QTimer>
#include <QTranslator>

#include <cstdio>

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
    const QCommandLineOption scanOption(QStringLiteral("scan"),
                                        QCoreApplication::translate("main", "Analyse les fichiers et dossiers donnés (menu de Dolphin, par exemple)."));
    const QCommandLineOption quickScanOption(QStringLiteral("quick-scan"),
                                             QCoreApplication::translate("main", "Lance une analyse rapide."));
    parser.addOption(socketOption);
    parser.addOption(backgroundOption);
    parser.addOption(scanOption);
    parser.addOption(quickScanOption);
    parser.addPositionalArgument(QStringLiteral("chemins"),
                                 QCoreApplication::translate("main", "Avec --scan : fichiers et dossiers à analyser."),
                                 QStringLiteral("[chemins…]"));
    parser.process(app);

    // Demande de cette invocation : afficher la fenêtre, analyser des
    // fichiers ou lancer une analyse rapide.
    InstanceRequest request;
    QStringList scanPaths;
    for (const QString &path : parser.positionalArguments())
        scanPaths << QFileInfo(path).absoluteFilePath(); // relatifs au dossier de lancement
    if (parser.isSet(scanOption) != !scanPaths.isEmpty()) {
        std::fprintf(stderr, "%s\n",
                     qPrintable(QCoreApplication::translate("main", "--scan attend un ou plusieurs fichiers ou dossiers, "
                                                                    "et des chemins ne vont qu'avec --scan.")));
        return 1;
    }
    if (!scanPaths.isEmpty()) {
        request.action = InstanceRequest::Action::Scan;
        request.paths = scanPaths;
    } else if (parser.isSet(quickScanOption)) {
        request.action = InstanceRequest::Action::QuickScan;
    }
    // Sous Wayland, le lanceur (Dolphin, menu des applications) fournit ce
    // jeton : il permet à la fenêtre de prendre le focus.
    request.activationToken = qEnvironmentVariable("XDG_ACTIVATION_TOKEN");

    // Déjà lancée ? On lui transmet la demande (réafficher sa fenêtre, sauf
    // lancement en arrière-plan, typiquement au démarrage de la session ;
    // analyser) et on quitte.
    const bool background = parser.isSet(backgroundOption) && request.action == InstanceRequest::Action::Show;
    SingleInstance instance(QStringLiteral("linux-defender"));
    if (!instance.tryBecomePrimary(background ? QByteArray() : request.encode()))
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
    PrivilegedHelper helper;
    SystemDiagnostics diagnostics(&watcher, &onAccess);
    MainWindow window(&watcher, &scans, &onAccess, &history, &diagnostics, &helper);
    TrayIcon tray(&watcher, &scans, &onAccess);

    const auto handleRequest = [&window](const InstanceRequest &request) {
        // Même mécanisme que pour les notifications : Qt lit ce jeton quand
        // la fenêtre demande à être activée.
        if (!request.activationToken.isEmpty())
            qputenv("XDG_ACTIVATION_TOKEN", request.activationToken.toUtf8());
        switch (request.action) {
        case InstanceRequest::Action::Show:
            window.showAndActivate();
            break;
        case InstanceRequest::Action::Scan:
            window.scanPaths(request.paths);
            break;
        case InstanceRequest::Action::QuickScan:
            window.startQuickScan();
            break;
        }
    };
    QObject::connect(&instance, &SingleInstance::messageReceived, &window, [handleRequest](const QByteArray &message) {
        if (const std::optional<InstanceRequest> request = InstanceRequest::decode(message))
            handleRequest(*request);
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
    // Correction du diagnostic : socket de clamd à détecter de nouveau (sa
    // configuration a pu changer), services à relire. clamd met quelques
    // secondes à redémarrer (chargement des signatures) : nouvelles
    // vérifications un peu plus tard.
    QObject::connect(&window, &MainWindow::systemChanged, &watcher, [&applySettings, &watcher, &onAccess] {
        applySettings();
        watcher.checkNow();
        onAccess.refresh();
        for (const int delay : {5000, 20000, 60000})
            QTimer::singleShot(delay, &watcher, &ClamdWatcher::checkNow);
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
    // Lancée pour analyser (Dolphin, --quick-scan) : l'analyse démarre tout de suite.
    if (request.action != InstanceRequest::Action::Show)
        handleRequest(request);

    watcher.start();
    // Protection en temps réel : état du service clamonacc, puis suivi de son journal.
    onAccess.refresh();
    onAccess.startMonitoring();
    diagnostics.refresh();
    return app.exec();
}
