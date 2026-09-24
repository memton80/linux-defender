#include "system/OnAccessController.h"
#include "system/OnAccessLog.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

// Faux systemd : une unité exposant les propriétés que lit OnAccessController.
class FakeUnit : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.systemd1.Unit")
    Q_PROPERTY(QString LoadState READ loadState)
    Q_PROPERTY(QString ActiveState READ activeState)
    Q_PROPERTY(QString SubState READ subState)

public:
    explicit FakeUnit(QObject *parent)
        : QDBusAbstractAdaptor(parent)
    {
    }
    QString loadState() const { return load; }
    QString activeState() const { return active; }
    QString subState() const { return sub; }

    QString load = QStringLiteral("not-found");
    QString active = QStringLiteral("inactive");
    QString sub = QStringLiteral("dead");
};

namespace
{
const QString kService = QString::fromLatin1(OnAccessController::kServiceName);

// Journaux réels de clamonacc 1.5.4 (lancé comme le service : --foreground
// --fdpass --wait), relevés dans les situations testées.
// Limite fs.inotify.max_user_watches trop basse : clamonacc quitte (code 0).
const QByteArray kRunWatchLimit = "--------------------------------------\n"
                                  "ClamInotif: watching '/home' (and all sub-directories)\n"
                                  "ERROR: ClamInotif: watch descriptor issue when adding watch for /home/alex/.git/info\n"
                                  "ERROR: ClamInotif: issue when adding watch for /home/alex/.git/info\n"
                                  "ERROR: ClamInotif: issue when adding watch for /home/alex/.git\n"
                                  "ERROR: ClamInotif: issue when adding watch for /home/alex\n"
                                  "ERROR: ClamInotif: could not watch path '/home', No space left on device\n"
                                  "ClamInotif: stopped\n"
                                  "ClamScanQueue: stopped\n";
const QString kWatchLimitError = QStringLiteral("ClamInotif: could not watch path '/home', No space left on device");
// clamd toujours injoignable après les 30 s de --wait : clamonacc quitte (code 21).
const QByteArray kRunClamdTimeout = "--------------------------------------\n"
                                    "ClamClient: Initial connection failed, Couldn't connect to server. Will try again...\n"
                                    "Wait timeout exceeded; Could not connect to clamd\n";
// Mise à jour Safe Browsing de Zen (Flatpak) : dossiers créés puis supprimés
// avant que clamonacc les surveille. Sans conséquence ; clamonacc continue.
const QByteArray kZenCacheRace =
    "ERROR: ClamInotif: could not add element to hash table for /home/alex/.var/app/app.zen_browser.zen/cache/zen/"
    "5alpemjo.Default (release)/safebrowsing-updating\n"
    "ERROR: ClamInotif: watch descriptor issue when adding watch for /home/alex/.var/app/app.zen_browser.zen/cache/zen/"
    "5alpemjo.Default (release)/safebrowsing-backup\n"
    "ERROR: ClamInotif: issue when adding watch for /home/alex/.var/app/app.zen_browser.zen/cache/zen/"
    "5alpemjo.Default (release)/safebrowsing-backup/google4\n";
const QString kDistributionService = QString::fromLatin1(OnAccessController::kDistributionServiceName);

void append(const QString &path, const QByteArray &text)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Append));
    file.write(text);
}

void writeFile(const QString &path, const QByteArray &content, QFileDevice::Permissions permissions = {})
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
    file.close();
    if (permissions)
        QVERIFY(file.setPermissions(permissions));
}
}

class TestOnAccess : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // Journal de clamonacc
    void parseLine_data();
    void parseLine();
    void historyThenLiveDetections();
    void fileCreatedLater();
    void directoryCreatedLater();
    void truncationAndRotation();
    void partialLine();
    void benignErrorsNotReported();
    void runStartReported();
    void historyReportsLastRunError();

    // Détection et diagnostic
    void findClamonacc();
    void installCommand_data();
    void installCommand();
    void readWatchedPaths();
    void explainError_data();
    void explainError();
    void unitObjectPath();
    void notInstalledWithoutBinary();
    void unreadableLogIsReported();
    void generatedFilesMatchCode();

    // État lu depuis systemd (faux systemd sur un bus de session privé)
    void stateFromSystemd_data();
    void stateFromSystemd();
    void failureExplainedByLog();
    void unexpectedStopExplainedByLog();
    void failureExplainedByHistory();
    void newRunForgetsPreviousError();
    void distributionServiceWarning();
    void refreshOnPropertiesChanged();
    void refreshAfterDaemonReload();
    void refreshAfterUnitFilesChanged();

private:
    void sendManagerSignal(const QString &name, const QVariantList &arguments = {});
    FakeUnit *unit(const QString &name);
    void requireFakeSystemd();
    // Contrôleur qui trouve un faux clamonacc et utilise le faux systemd.
    std::unique_ptr<OnAccessController> controller(const QString &logPath = {});

    QTemporaryDir m_dir;
    QString m_binDir;
    QDBusConnection m_fake{QString()};
    QHash<QString, FakeUnit *> m_units;
};

void TestOnAccess::initTestCase()
{
    m_binDir = m_dir.filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkpath(m_binDir));
    writeFile(m_binDir + QStringLiteral("/clamonacc"), "#!/bin/sh\n",
              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    if (!QDBusConnection::sessionBus().isConnected())
        return; // tests systemd ignorés (QSKIP)
    m_fake = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("fake-systemd"));
    QVERIFY(m_fake.registerService(QStringLiteral("org.freedesktop.systemd1")));
    unit(kService);
    unit(kDistributionService);
}

FakeUnit *TestOnAccess::unit(const QString &name)
{
    if (!m_units.contains(name)) {
        auto *object = new QObject(this);
        m_units.insert(name, new FakeUnit(object));
        m_fake.registerObject(OnAccessController::unitObjectPath(name), object, QDBusConnection::ExportAdaptors);
    }
    return m_units.value(name);
}

void TestOnAccess::requireFakeSystemd()
{
    if (!m_fake.isConnected())
        QSKIP("Pas de bus de session D-Bus : lancer le test avec dbus-run-session.");
}

std::unique_ptr<OnAccessController> TestOnAccess::controller(const QString &logPath)
{
    auto result = std::make_unique<OnAccessController>(
        QDBusConnection::sessionBus(), logPath.isEmpty() ? m_dir.filePath(QStringLiteral("absent.log")) : logPath);
    result->setSearchDirectories({m_binDir});
    return result;
}

// --- Journal -----------------------------------------------------------------

void TestOnAccess::parseLine_data()
{
    QTest::addColumn<QString>("line");
    QTest::addColumn<QString>("kind"); // vide : ligne ignorée
    QTest::addColumn<QString>("path");
    QTest::addColumn<QString>("threat");
    QTest::addColumn<QString>("message");

    // Lignes relevées sur clamonacc 1.5.4 (--fdpass).
    QTest::newRow("détection") << "/home/u/facture.pdf.exe: Win.Test.EICAR_HDB-1 FOUND" << "détection"
                               << "/home/u/facture.pdf.exe" << "Win.Test.EICAR_HDB-1" << QString();
    QTest::newRow("« : » dans le nom")
        << "/tmp/w/Mon dossier/facture: mars.pdf.exe: LinuxDefender.Test.Signature.UNOFFICIAL FOUND" << "détection"
        << "/tmp/w/Mon dossier/facture: mars.pdf.exe" << "LinuxDefender.Test.Signature.UNOFFICIAL" << QString();
    QTest::newRow("date en préfixe") << "Wed Sep 24 10:12:13 2026 -> /home/u/a.exe: Eicar FOUND" << "détection"
                                     << "/home/u/a.exe" << "Eicar" << QString();
    QTest::newRow("début de lancement") << "--------------------------------------" << "lancement" << QString()
                                        << QString() << QString();
    QTest::newRow("erreur fanotify") << "ERROR: Clamonacc: fanotify_init failed: Operation not permitted" << "erreur"
                                     << QString() << QString()
                                     << "Clamonacc: fanotify_init failed: Operation not permitted";
    QTest::newRow("limite inotify") << QStringLiteral("ERROR: ") + kWatchLimitError << "erreur" << QString() << QString()
                                    << kWatchLimitError;
    QTest::newRow("clamd injoignable, sans ERROR:")
        << "Wait timeout exceeded; Could not connect to clamd" << "erreur" << QString() << QString()
        << "Wait timeout exceeded; Could not connect to clamd";
    // Dossier supprimé avant d'être surveillé (caches de navigateur).
    QTest::newRow("hash table") << "ERROR: ClamInotif: could not add element to hash table for /home/u/.cache/x"
                                << "bénigne" << QString() << QString()
                                << "ClamInotif: could not add element to hash table for /home/u/.cache/x";
    QTest::newRow("watch descriptor")
        << "ERROR: ClamInotif: watch descriptor issue when adding watch for /home/u/sb (release)/safebrowsing-backup"
        << "bénigne" << QString() << QString()
        << "ClamInotif: watch descriptor issue when adding watch for /home/u/sb (release)/safebrowsing-backup";
    QTest::newRow("issue when adding") << "ERROR: ClamInotif: issue when adding watch for /home/u/.cache/x/y"
                                       << "bénigne" << QString() << QString()
                                       << "ClamInotif: issue when adding watch for /home/u/.cache/x/y";
    QTest::newRow("surveillance") << "ClamInotif: watching '/home' (and all sub-directories)" << QString()
                                  << QString() << QString() << QString();
    QTest::newRow("nouvelle tentative")
        << "ClamClient: Initial connection failed, Couldn't connect to server. Will try again..." << QString()
        << QString() << QString() << QString();
    QTest::newRow("arrêt") << "ClamInotif: stopped" << QString() << QString() << QString() << QString();
    QTest::newRow("FOUND sans chemin") << "FOUND" << QString() << QString() << QString() << QString();
}

void TestOnAccess::parseLine()
{
    QFETCH(QString, line);
    QFETCH(QString, kind);
    QFETCH(QString, path);
    QFETCH(QString, threat);
    QFETCH(QString, message);

    const std::optional<OnAccessLogLine> parsed = OnAccessLog::parseLine(line);
    QCOMPARE(parsed.has_value(), !kind.isEmpty());
    if (!parsed)
        return;
    QString parsedKind;
    switch (parsed->type) {
    case OnAccessLogLine::Type::Detection:
        parsedKind = QStringLiteral("détection");
        break;
    case OnAccessLogLine::Type::RunStart:
        parsedKind = QStringLiteral("lancement");
        break;
    case OnAccessLogLine::Type::Error:
        parsedKind = parsed->benign ? QStringLiteral("bénigne") : QStringLiteral("erreur");
        break;
    }
    QCOMPARE(parsedKind, kind);
    QCOMPARE(parsed->path, path);
    QCOMPARE(parsed->threat, threat);
    QCOMPARE(parsed->message, message);
}

void TestOnAccess::historyThenLiveDetections()
{
    const QString path = m_dir.filePath(QStringLiteral("history.log"));
    // clamonacc écrit souvent deux fois la même détection : un seul élément attendu.
    writeFile(path, "--------------------------------------\n"
                    "ClamInotif: watching '/home' (and all sub-directories)\n"
                    "/home/u/ancien.exe: Old.Threat FOUND\n"
                    "/home/u/ancien.exe: Old.Threat FOUND\n");

    OnAccessLog log(path);
    QSignalSpy history(&log, &OnAccessLog::historyLoaded);
    QSignalSpy detected(&log, &OnAccessLog::threatDetected);
    QSignalSpy errors(&log, &OnAccessLog::errorLogged);
    log.start();

    QCOMPARE(history.count(), 1);
    const auto past = history.first().first().value<QList<OnAccessDetection>>();
    QCOMPARE(past.size(), 1);
    QCOMPARE(past.first().path, QStringLiteral("/home/u/ancien.exe"));
    QVERIFY(!past.first().time.isValid()); // heure inconnue : avant le lancement

    append(path, "/home/u/nouveau.exe: New.Threat FOUND\n/home/u/nouveau.exe: New.Threat FOUND\n");
    QVERIFY(detected.wait(3000));
    QTest::qWait(300);
    QCOMPARE(detected.count(), 1); // doublon ignoré
    const auto detection = detected.first().first().value<OnAccessDetection>();
    QCOMPARE(detection.path, QStringLiteral("/home/u/nouveau.exe"));
    QCOMPARE(detection.threat, QStringLiteral("New.Threat"));
    QVERIFY(detection.time.isValid());

    append(path, "ERROR: ClamClient: Could not connect to clamd, Couldn't connect to server\n");
    QVERIFY(errors.wait(3000));
    QCOMPARE(errors.first().first().toString(), QStringLiteral("ClamClient: Could not connect to clamd, Couldn't connect to server"));
}

void TestOnAccess::fileCreatedLater()
{
    const QString path = m_dir.filePath(QStringLiteral("later.log"));
    OnAccessLog log(path);
    QSignalSpy history(&log, &OnAccessLog::historyLoaded);
    QSignalSpy detected(&log, &OnAccessLog::threatDetected);
    log.start();
    QVERIFY(history.first().first().value<QList<OnAccessDetection>>().isEmpty());

    append(path, "/home/u/a.exe: Threat.A FOUND\n");
    QVERIFY(detected.wait(3000));
    QCOMPARE(detected.first().first().value<OnAccessDetection>().threat, QStringLiteral("Threat.A"));
}

void TestOnAccess::directoryCreatedLater()
{
    // Comme /var/log/linux-defender avant le premier lancement du service.
    const QString directory = m_dir.filePath(QStringLiteral("logs/linux-defender"));
    const QString path = directory + QStringLiteral("/clamonacc.log");
    OnAccessLog log(path);
    QSignalSpy detected(&log, &OnAccessLog::threatDetected);
    log.start();

    QVERIFY(QDir().mkpath(directory));
    QTest::qWait(200); // le temps de passer à la surveillance du nouveau dossier
    append(path, "/home/u/b.exe: Threat.B FOUND\n");
    QTRY_COMPARE_WITH_TIMEOUT(detected.count(), 1, 3000);
}

void TestOnAccess::truncationAndRotation()
{
    const QString path = m_dir.filePath(QStringLiteral("rotation.log"));
    writeFile(path, "/home/u/1.exe: Threat.1 FOUND\n");
    OnAccessLog log(path);
    QSignalSpy detected(&log, &OnAccessLog::threatDetected);
    log.start();

    // Fichier vidé puis réécrit (logrotate avec copytruncate).
    writeFile(path, "/home/u/2.exe: Threat.2 FOUND\n");
    QTRY_COMPARE_WITH_TIMEOUT(detected.count(), 1, 3000);
    QCOMPARE(detected.last().first().value<OnAccessDetection>().threat, QStringLiteral("Threat.2"));

    // Fichier renommé puis recréé (rotation classique).
    QVERIFY(QFile::rename(path, path + QStringLiteral(".1")));
    QTest::qWait(200);
    writeFile(path, "/home/u/3.exe: Threat.3 FOUND\n");
    QTRY_COMPARE_WITH_TIMEOUT(detected.count(), 2, 3000);
    QCOMPARE(detected.last().first().value<OnAccessDetection>().threat, QStringLiteral("Threat.3"));
}

void TestOnAccess::partialLine()
{
    const QString path = m_dir.filePath(QStringLiteral("partial.log"));
    writeFile(path, "");
    OnAccessLog log(path);
    QSignalSpy detected(&log, &OnAccessLog::threatDetected);
    log.start();

    append(path, "/home/u/c.exe: Thre");
    QVERIFY(!detected.wait(500)); // ligne pas encore terminée
    append(path, "at.C FOUND\n");
    QVERIFY(detected.wait(3000));
    QCOMPARE(detected.first().first().value<OnAccessDetection>().threat, QStringLiteral("Threat.C"));
}

void TestOnAccess::benignErrorsNotReported()
{
    const QString path = m_dir.filePath(QStringLiteral("benign.log"));
    writeFile(path, "");
    OnAccessLog log(path);
    QSignalSpy errors(&log, &OnAccessLog::errorLogged);
    QSignalSpy detected(&log, &OnAccessLog::threatDetected);
    log.start();

    // Erreurs sans conséquence : clamonacc continue de protéger.
    append(path, kZenCacheRace);
    append(path, "/home/alex/eicar.com: Win.Test.EICAR_HDB-1 FOUND\n");
    QVERIFY(detected.wait(3000));
    QCOMPARE(errors.count(), 0);

    // Les mêmes lignes précèdent l'erreur fatale : seule celle-ci est signalée.
    append(path, kRunWatchLimit);
    QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 3000);
    QCOMPARE(errors.first().first().toString(), kWatchLimitError);
}

void TestOnAccess::runStartReported()
{
    const QString path = m_dir.filePath(QStringLiteral("runs.log"));
    writeFile(path, "");
    OnAccessLog log(path);
    QSignalSpy runs(&log, &OnAccessLog::runStarted);
    QSignalSpy errors(&log, &OnAccessLog::errorLogged);
    log.start();

    append(path, kRunClamdTimeout);
    QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 3000);
    QCOMPARE(runs.count(), 1);
    QCOMPARE(errors.first().first().toString(), QStringLiteral("Wait timeout exceeded; Could not connect to clamd"));
}

void TestOnAccess::historyReportsLastRunError()
{
    // Le service a échoué avant le lancement de l'application : la cause est
    // la dernière erreur du dernier lancement, pas celle d'un lancement précédent.
    const QString path = m_dir.filePath(QStringLiteral("history-error.log"));
    writeFile(path, kRunClamdTimeout + kRunWatchLimit);
    {
        OnAccessLog log(path);
        QSignalSpy errors(&log, &OnAccessLog::errorLogged);
        log.start();
        QCOMPARE(errors.count(), 1);
        QCOMPARE(errors.first().first().toString(), kWatchLimitError);
    }

    // Dernier lancement sans erreur (erreurs bénignes seulement) : rien à signaler.
    writeFile(path, kRunWatchLimit + "--------------------------------------\n"
                                     "ClamInotif: watching '/home' (and all sub-directories)\n"
                    + kZenCacheRace);
    OnAccessLog log(path);
    QSignalSpy errors(&log, &OnAccessLog::errorLogged);
    log.start();
    QCOMPARE(errors.count(), 0);
}

// --- Détection et diagnostic -------------------------------------------------

void TestOnAccess::findClamonacc()
{
    const QString empty = m_dir.filePath(QStringLiteral("empty"));
    QVERIFY(QDir().mkpath(empty));
    QCOMPARE(OnAccessController::findClamonacc({empty, m_binDir}), m_binDir + QStringLiteral("/clamonacc"));
    QVERIFY(OnAccessController::findClamonacc({empty}).isEmpty());
}

void TestOnAccess::installCommand_data()
{
    QTest::addColumn<QByteArray>("osRelease");
    QTest::addColumn<QString>("command");

    QTest::newRow("Fedora") << QByteArray("NAME=\"Fedora Linux\"\nID=fedora\nVERSION_ID=44\n")
                            << "sudo dnf install clamav clamd";
    QTest::newRow("Ubuntu") << QByteArray("ID=ubuntu\nID_LIKE=debian\n") << "sudo apt install clamav-daemon";
    QTest::newRow("Debian") << QByteArray("ID=debian\n") << "sudo apt install clamav-daemon";
    QTest::newRow("Arch") << QByteArray("ID=arch\n") << "sudo pacman -S clamav";
    QTest::newRow("openSUSE") << QByteArray("ID=\"opensuse-tumbleweed\"\nID_LIKE=\"opensuse suse\"\n")
                              << "sudo zypper install clamav";
    QTest::newRow("inconnue") << QByteArray("ID=autre\n") << QString();
}

void TestOnAccess::installCommand()
{
    QFETCH(QByteArray, osRelease);
    QFETCH(QString, command);
    const QString path = m_dir.filePath(QStringLiteral("os-release"));
    writeFile(path, osRelease);
    QCOMPARE(OnAccessController::installCommand(path), command);
}

void TestOnAccess::readWatchedPaths()
{
    const QString path = m_dir.filePath(QStringLiteral("clamonacc.conf"));
    writeFile(path, "LocalSocket /run/clamav/clamd.ctl\n"
                    "OnAccessIncludePath /home\n"
                    "#OnAccessIncludePath /commenté\n"
                    "  OnAccessIncludePath /srv/partage commun\n"
                    "OnAccessExcludeUname clamav\n");
    QCOMPARE(OnAccessController::readWatchedPaths(path),
             QStringList({QStringLiteral("/home"), QStringLiteral("/srv/partage commun")}));
}

void TestOnAccess::explainError_data()
{
    QTest::addColumn<QString>("error");
    QTest::addColumn<QString>("expected");

    // Messages relevés sur clamonacc 1.5.4.
    QTest::newRow("privilèges") << "Clamonacc: fanotify_init failed: Operation not permitted" << "privilèges";
    QTest::newRow("clamd") << "ClamClient: Could not connect to clamd, Couldn't connect to server" << "joindre clamd";
    QTest::newRow("clamd, fin de --wait") << "Wait timeout exceeded; Could not connect to clamd" << "joindre clamd";
    QTest::newRow("limite inotify") << kWatchLimitError << "max_user_watches";
    QTest::newRow("autre") << "Something else" << "Something else";
}

void TestOnAccess::explainError()
{
    QFETCH(QString, error);
    QFETCH(QString, expected);
    const QString explanation = OnAccessController::explainError(error);
    QVERIFY2(explanation.contains(expected), qPrintable(explanation));
}

void TestOnAccess::unitObjectPath()
{
    QCOMPARE(OnAccessController::unitObjectPath(kService),
             QStringLiteral("/org/freedesktop/systemd1/unit/linux_2ddefender_2donaccess_2eservice"));
    QCOMPARE(OnAccessController::unitObjectPath(QStringLiteral("2x.service")),
             QStringLiteral("/org/freedesktop/systemd1/unit/_32x_2eservice"));
}

void TestOnAccess::notInstalledWithoutBinary()
{
    OnAccessController controller(QDBusConnection(QStringLiteral("aucun-bus")), m_dir.filePath(QStringLiteral("x.log")));
    controller.setSearchDirectories({m_dir.filePath(QStringLiteral("empty"))});
    controller.refresh();
    QCOMPARE(controller.state(), OnAccessController::State::NotInstalled);
    QVERIFY(controller.message().contains(QStringLiteral("clamonacc n'est pas installé")));
}

void TestOnAccess::unreadableLogIsReported()
{
    if (::geteuid() == 0)
        QSKIP("root ignore les permissions des fichiers");
    // ClamAV crée son journal en 0640 root:root : illisible pour l'utilisateur.
    const QString logPath = m_dir.filePath(QStringLiteral("root-only.log"));
    writeFile(logPath, "", QFileDevice::WriteOwner);

    OnAccessController controller(QDBusConnection(QStringLiteral("aucun-bus")), logPath);
    controller.setSearchDirectories({m_binDir});
    controller.refresh();
    QVERIFY2(controller.message().contains(QStringLiteral("n'est pas lisible")), qPrintable(controller.message()));
}

void TestOnAccess::generatedFilesMatchCode()
{
    // Fichiers installés par les paquets, générés par CMake à partir des mêmes
    // valeurs que le code : ce test garantit qu'ils ne divergent jamais.
    const QString dir = QStringLiteral(DEFENDER_ONACCESS_GENERATED_DIR);
    const QString config = QString::fromLatin1(OnAccessController::kConfigPath);
    const QString log = QString::fromLatin1(OnAccessController::kLogPath);

    // Service : le nom du fichier est exactement celui que cherche l'application.
    QFile unit(dir + QLatin1Char('/') + kService);
    QVERIFY2(unit.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(unit.fileName()));
    const QString service = QString::fromUtf8(unit.readAll());
    QVERIFY(!service.contains(QLatin1String("@DEFENDER"))); // toutes les variables remplacées
    QVERIFY(service.contains(QStringLiteral("--config-file=") + config));
    QVERIFY(service.contains(QStringLiteral("--log=") + log));
    QVERIFY(service.contains(QLatin1String("--fdpass")));
    QVERIFY(service.contains(QStringLiteral("Conflicts=") + kDistributionService));
    // LogsDirectory=<nom> crée /var/log/<nom> : ce doit être le dossier du journal.
    QCOMPARE(QFileInfo(log).absolutePath(), QStringLiteral("/var/log/linux-defender"));
    QVERIFY(service.contains(QLatin1String("LogsDirectory=linux-defender")));
    // Installé désactivé, mais activable (section [Install]).
    QVERIFY(service.contains(QLatin1String("WantedBy=multi-user.target")));
    // clamonacc quitte avec le code 0 même sur une erreur fatale : seul
    // Restart=always le relance (et fait apparaître « auto-restart »).
    QVERIFY(service.contains(QLatin1String("\nRestart=always\n")));
    // Sans clamonacc (seulement recommandé), le service est ignoré, pas relancé en boucle.
    QVERIFY(service.contains(QLatin1String("\nConditionPathExists=/")));
    // clamonacc ignore SIGHUP : un ExecReload qui l'envoie ne servirait à rien.
    QVERIFY(!service.contains(QLatin1String("\nExecReload=")));

    // Configuration de clamonacc : dossiers surveillés lus par l'application.
    const QString configFile = dir + QStringLiteral("/clamonacc.conf");
    QCOMPARE(OnAccessController::readWatchedPaths(configFile), QStringList{QStringLiteral("/home")});
    QFile conf(configFile);
    QVERIFY(conf.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString confText = QString::fromUtf8(conf.readAll());
    QVERIFY(!confText.contains(QLatin1String("@DEFENDER")));
    QVERIFY(confText.contains(QLatin1String("\nOnAccessPrevention no\n")));
    // Fichiers analysés aussi à l'écriture, pas seulement à l'ouverture.
    QVERIFY(confText.contains(QLatin1String("\nOnAccessExtraScanning yes\n")));

    // Rotation : même journal, vidé en place. clamonacc ne rouvre jamais son
    // journal : renommé (« create »), il continuerait d'écrire dans l'ancien.
    QFile rotate(dir + QStringLiteral("/linux-defender.logrotate"));
    QVERIFY(rotate.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString rotateText = QString::fromUtf8(rotate.readAll());
    QVERIFY(!rotateText.contains(QLatin1String("@DEFENDER")));
    QVERIFY(rotateText.startsWith(QLatin1String("#")) && rotateText.contains(log + QStringLiteral(" {")));
    QVERIFY(rotateText.contains(QLatin1String("\n    copytruncate\n")));
    QVERIFY(!rotateText.contains(QLatin1String("\n    create")));
    QVERIFY(!rotateText.contains(QLatin1String("postrotate")));
}

// --- État lu depuis systemd --------------------------------------------------

void TestOnAccess::stateFromSystemd_data()
{
    QTest::addColumn<QString>("load");
    QTest::addColumn<QString>("active");
    QTest::addColumn<QString>("sub");
    QTest::addColumn<OnAccessController::State>("state");

    QTest::newRow("service absent") << "not-found" << "inactive" << "dead" << OnAccessController::State::ServiceMissing;
    QTest::newRow("arrêté") << "loaded" << "inactive" << "dead" << OnAccessController::State::Inactive;
    QTest::newRow("démarrage") << "loaded" << "activating" << "start" << OnAccessController::State::Unknown;
    QTest::newRow("actif") << "loaded" << "active" << "running" << OnAccessController::State::Active;
    QTest::newRow("en échec") << "loaded" << "failed" << "failed" << OnAccessController::State::Failed;
    // Arrêt que personne n'a demandé : systemd relance clamonacc (Restart=always).
    QTest::newRow("relance") << "loaded" << "activating" << "auto-restart" << OnAccessController::State::Failed;
    QTest::newRow("relance en file") << "loaded" << "activating" << "auto-restart-queued"
                                     << OnAccessController::State::Failed;
    QTest::newRow("avant relance") << "loaded" << "inactive" << "dead-before-auto-restart"
                                   << OnAccessController::State::Failed;
}

void TestOnAccess::stateFromSystemd()
{
    requireFakeSystemd();
    QFETCH(QString, load);
    QFETCH(QString, active);
    QFETCH(QString, sub);
    QFETCH(OnAccessController::State, state);

    unit(kService)->load = load;
    unit(kService)->active = active;
    unit(kService)->sub = sub;
    unit(kDistributionService)->active = QStringLiteral("inactive");

    auto onAccess = controller();
    onAccess->refresh();
    // Message vide jusqu'à la réponse de systemd (l'état initial est Unknown).
    QTRY_VERIFY_WITH_TIMEOUT(!onAccess->message().isEmpty(), 3000);
    QCOMPARE(onAccess->state(), state);
    unit(kService)->sub = QStringLiteral("dead");
}

void TestOnAccess::failureExplainedByLog()
{
    requireFakeSystemd();
    unit(kService)->load = QStringLiteral("loaded");
    unit(kService)->active = QStringLiteral("failed");
    unit(kDistributionService)->active = QStringLiteral("inactive");

    const QString logPath = m_dir.filePath(QStringLiteral("failed.log"));
    writeFile(logPath, "");
    auto onAccess = controller(logPath);
    onAccess->startMonitoring();
    onAccess->refresh();
    QTRY_COMPARE_WITH_TIMEOUT(onAccess->state(), OnAccessController::State::Failed, 3000);

    append(logPath, "ERROR: Clamonacc: fanotify_init failed: Operation not permitted\n");
    QTRY_VERIFY_WITH_TIMEOUT(onAccess->message().contains(QLatin1String("CAP_SYS_ADMIN")), 3000);
}

void TestOnAccess::unexpectedStopExplainedByLog()
{
    requireFakeSystemd();
    // Limite inotify atteinte : clamonacc quitte avec le code 0, comme lors
    // d'un arrêt demandé. systemd, lui, sait que l'arrêt n'a pas été demandé.
    unit(kService)->load = QStringLiteral("loaded");
    unit(kService)->active = QStringLiteral("activating");
    unit(kService)->sub = QStringLiteral("auto-restart");
    unit(kDistributionService)->active = QStringLiteral("inactive");

    const QString logPath = m_dir.filePath(QStringLiteral("auto-restart.log"));
    writeFile(logPath, "");
    auto onAccess = controller(logPath);
    onAccess->startMonitoring();
    onAccess->refresh();
    QTRY_COMPARE_WITH_TIMEOUT(onAccess->state(), OnAccessController::State::Failed, 3000);
    QVERIFY2(onAccess->message().contains(QStringLiteral("s'est arrêté de lui-même")), qPrintable(onAccess->message()));
    QVERIFY(onAccess->message().contains(QLatin1String("relance automatiquement")));

    append(logPath, kRunWatchLimit);
    QTRY_VERIFY_WITH_TIMEOUT(onAccess->message().contains(QLatin1String("max_user_watches")), 3000);
    QVERIFY(onAccess->message().contains(QLatin1String("relance automatiquement")));

    // Erreurs bénignes écrites ensuite : l'explication reste celle de l'erreur fatale.
    append(logPath, kZenCacheRace);
    QTest::qWait(500);
    QVERIFY2(onAccess->message().contains(QLatin1String("max_user_watches")), qPrintable(onAccess->message()));
    unit(kService)->sub = QStringLiteral("dead");
}

void TestOnAccess::failureExplainedByHistory()
{
    requireFakeSystemd();
    // L'application démarre alors que le service a déjà échoué.
    unit(kService)->load = QStringLiteral("loaded");
    unit(kService)->active = QStringLiteral("activating");
    unit(kService)->sub = QStringLiteral("auto-restart");
    unit(kDistributionService)->active = QStringLiteral("inactive");

    const QString logPath = m_dir.filePath(QStringLiteral("history-failed.log"));
    writeFile(logPath, kRunClamdTimeout);
    auto onAccess = controller(logPath);
    onAccess->startMonitoring();
    onAccess->refresh();
    QTRY_COMPARE_WITH_TIMEOUT(onAccess->state(), OnAccessController::State::Failed, 3000);
    QVERIFY2(onAccess->message().contains(QLatin1String("joindre clamd")), qPrintable(onAccess->message()));
    unit(kService)->sub = QStringLiteral("dead");
}

void TestOnAccess::newRunForgetsPreviousError()
{
    requireFakeSystemd();
    unit(kService)->load = QStringLiteral("loaded");
    unit(kService)->active = QStringLiteral("failed");
    unit(kService)->sub = QStringLiteral("failed");
    unit(kDistributionService)->active = QStringLiteral("inactive");

    const QString logPath = m_dir.filePath(QStringLiteral("new-run.log"));
    writeFile(logPath, kRunWatchLimit);
    auto onAccess = controller(logPath);
    onAccess->startMonitoring();
    onAccess->refresh();
    QTRY_VERIFY_WITH_TIMEOUT(onAccess->message().contains(QLatin1String("max_user_watches")), 3000);

    // clamonacc relancé : l'erreur du lancement précédent n'explique plus rien.
    append(logPath, "--------------------------------------\n");
    QTRY_VERIFY_WITH_TIMEOUT(onAccess->message().contains(QLatin1String("journalctl")), 3000);
    QVERIFY(!onAccess->message().contains(QLatin1String("max_user_watches")));
    unit(kService)->sub = QStringLiteral("dead");
}

void TestOnAccess::distributionServiceWarning()
{
    requireFakeSystemd();
    unit(kService)->load = QStringLiteral("loaded");
    unit(kService)->active = QStringLiteral("active");
    unit(kDistributionService)->active = QStringLiteral("active");

    auto onAccess = controller();
    onAccess->refresh();
    QTRY_VERIFY_WITH_TIMEOUT(onAccess->message().contains(kDistributionService), 3000);
    QCOMPARE(onAccess->state(), OnAccessController::State::Active);
    unit(kDistributionService)->active = QStringLiteral("inactive");
}

void TestOnAccess::refreshOnPropertiesChanged()
{
    requireFakeSystemd();
    unit(kService)->load = QStringLiteral("loaded");
    unit(kService)->active = QStringLiteral("inactive");
    unit(kDistributionService)->active = QStringLiteral("inactive");

    auto onAccess = controller();
    onAccess->refresh();
    QTRY_COMPARE_WITH_TIMEOUT(onAccess->state(), OnAccessController::State::Inactive, 3000);

    // Le service démarre : systemd le signale, le contrôleur relit l'état.
    unit(kService)->active = QStringLiteral("active");
    QDBusMessage signal = QDBusMessage::createSignal(OnAccessController::unitObjectPath(kService),
                                                     QStringLiteral("org.freedesktop.DBus.Properties"),
                                                     QStringLiteral("PropertiesChanged"));
    signal << QStringLiteral("org.freedesktop.systemd1.Unit") << QVariantMap() << QStringList();
    QVERIFY(m_fake.send(signal));
    QTRY_COMPARE_WITH_TIMEOUT(onAccess->state(), OnAccessController::State::Active, 3000);
}

void TestOnAccess::sendManagerSignal(const QString &name, const QVariantList &arguments)
{
    QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/org/freedesktop/systemd1"),
                                                     QStringLiteral("org.freedesktop.systemd1.Manager"), name);
    signal.setArguments(arguments);
    QVERIFY(m_fake.send(signal));
}

void TestOnAccess::refreshAfterDaemonReload()
{
    requireFakeSystemd();
    unit(kService)->load = QStringLiteral("not-found");
    unit(kService)->active = QStringLiteral("inactive");
    unit(kDistributionService)->active = QStringLiteral("inactive");

    auto onAccess = controller();
    onAccess->refresh();
    QTRY_COMPARE_WITH_TIMEOUT(onAccess->state(), OnAccessController::State::ServiceMissing, 3000);

    // Le paquet installe le service puis lance systemctl daemon-reload :
    // l'application doit le voir sans redémarrer.
    unit(kService)->load = QStringLiteral("loaded");
    sendManagerSignal(QStringLiteral("Reloading"), {true});
    sendManagerSignal(QStringLiteral("Reloading"), {false});
    QTRY_COMPARE_WITH_TIMEOUT(onAccess->state(), OnAccessController::State::Inactive, 3000);
}

void TestOnAccess::refreshAfterUnitFilesChanged()
{
    requireFakeSystemd();
    unit(kService)->load = QStringLiteral("loaded");
    unit(kService)->active = QStringLiteral("inactive");
    unit(kDistributionService)->active = QStringLiteral("inactive");

    auto onAccess = controller();
    onAccess->refresh();
    QTRY_COMPARE_WITH_TIMEOUT(onAccess->state(), OnAccessController::State::Inactive, 3000);

    // systemctl enable --now : systemd signale le changement des fichiers d'unités.
    unit(kService)->active = QStringLiteral("active");
    sendManagerSignal(QStringLiteral("UnitFilesChanged"));
    QTRY_COMPARE_WITH_TIMEOUT(onAccess->state(), OnAccessController::State::Active, 3000);
}

QTEST_GUILESS_MAIN(TestOnAccess)
#include "tst_onaccess.moc"
