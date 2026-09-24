#include "core/ClamdClient.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

// Étape 1 : test de communication avec clamd (PING) en ligne de commande.
// Ce point d'entrée deviendra l'application graphique (QApplication + icône
// dans la zone de notification) à l'étape 2.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("linux-dedender"));
    QCoreApplication::setApplicationVersion(QStringLiteral(DEDENDER_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "Vérifie la communication avec clamd (PING)."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption socketOption({QStringLiteral("s"), QStringLiteral("socket")},
                                          QCoreApplication::translate("main", "Chemin du socket clamd (détecté automatiquement par défaut)."),
                                          QCoreApplication::translate("main", "chemin"));
    parser.addOption(socketOption);
    parser.process(app);

    ClamdClient client;
    if (parser.isSet(socketOption))
        client.setSocketPath(parser.value(socketOption));

    QTextStream out(stdout);
    QTextStream err(stderr);

    QObject::connect(&client, &ClamdClient::pong, &app, [&] {
        out << "clamd OK (PONG) via " << client.socketPath() << Qt::endl;
        app.exit(0);
    });
    QObject::connect(&client, &ClamdClient::errorOccurred, &app, [&](ClamdClient::Error, const QString &message) {
        err << message << Qt::endl;
        app.exit(1);
    });

    client.ping();
    return app.exec();
}
