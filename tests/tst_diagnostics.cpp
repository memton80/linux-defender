#include "system/PrivilegedHelper.h"
#include "system/SystemDiagnostics.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace
{
void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

// clamd qui répond, signatures du jour, rien à signaler.
DiagnosticInput healthy()
{
    DiagnosticInput input;
    input.clamdState = ClamdWatcher::State::Connected;
    input.version = ClamdVersion{QStringLiteral("1.5.4"), QStringLiteral("27775"), QDateTime::currentDateTime()};
    input.socketPath = QStringLiteral("/run/clamav/clamd.ctl");
    input.config = ClamdConfig::parse(QStringLiteral("LocalSocket /run/clamav/clamd.ctl\nAlertExceedsMax yes\n"));
    input.config.path = QStringLiteral("/etc/clamav/clamd.conf");
    input.clamdService = QStringLiteral("clamav-daemon.service");
    input.clamdServiceState = QStringLiteral("active");
    input.freshclamInstalled = true;
    input.freshclamActive = true;
    input.distribution = {QStringLiteral("ubuntu"), QStringLiteral("debian")};
    input.userName = QStringLiteral("alex");
    input.socketGroup = QStringLiteral("clamav");
    input.userInSocketGroup = true;
    input.sessionHasSocketGroup = true;
    input.onAccessState = OnAccessController::State::Active;
    input.inotifyMaxWatches = 1048576;
    return input;
}

DiagnosticInput clamdDown(ClamdClient::Error error)
{
    DiagnosticInput input = healthy();
    input.clamdState = ClamdWatcher::State::Error;
    input.clamdError = error;
    input.clamdErrorMessage = QStringLiteral("clamd ne répond pas");
    input.version = {};
    return input;
}

DiagnosticItem item(const DiagnosticInput &input, const QString &id)
{
    for (const DiagnosticItem &item : SystemDiagnostics::evaluate(input)) {
        if (item.id == id)
            return item;
    }
    DiagnosticItem absent;
    absent.id = QStringLiteral("<absent>");
    return absent;
}

bool has(const DiagnosticInput &input, const QString &id)
{
    return item(input, id).id == id;
}
}

class TestDiagnostics : public QObject
{
    Q_OBJECT

private slots:
    void healthySystem();
    void clamdStopped_data();
    void clamdStopped();
    void fedoraExampleLine();
    void fedoraSocketDisabled();
    void clamdNotInstalled();
    void permissionDenied();
    void sessionToReopen();
    void signatures();
    void selinux();
    void alertExceedsMax();
    void onAccess();
    void inotify();
    void readsSystemFiles();
    void helperArguments();
    void helperExitCodes();
    void helperRunsCommand();
};

void TestDiagnostics::healthySystem()
{
    const QList<DiagnosticItem> items = SystemDiagnostics::evaluate(healthy());
    QStringList ids;
    for (const DiagnosticItem &item : items) {
        ids << item.id;
        QCOMPARE(int(item.level), int(DiagnosticItem::Level::Ok));
        QVERIFY(!item.fix);
    }
    // Sans SELinux, pas de vérification SELinux.
    QCOMPARE(ids, (QStringList{QStringLiteral("clamd"), QStringLiteral("signatures"), QStringLiteral("limits"),
                               QStringLiteral("onaccess"), QStringLiteral("inotify")}));
}

void TestDiagnostics::clamdStopped_data()
{
    QTest::addColumn<QString>("state");
    QTest::addColumn<int>("level");
    QTest::addColumn<bool>("fixable");

    QTest::newRow("arrêté") << "inactive" << int(DiagnosticItem::Level::Error) << true;
    QTest::newRow("en échec") << "failed" << int(DiagnosticItem::Level::Error) << true;
    QTest::newRow("démarrage") << "activating" << int(DiagnosticItem::Level::Info) << false;
    // Démarré, mais pas sur le socket utilisé : rien à corriger automatiquement.
    QTest::newRow("autre socket") << "active" << int(DiagnosticItem::Level::Error) << false;
}

void TestDiagnostics::clamdStopped()
{
    QFETCH(QString, state);
    QFETCH(int, level);
    QFETCH(bool, fixable);

    DiagnosticInput input = clamdDown(ClamdClient::Error::ConnectionRefused);
    input.clamdServiceState = state;
    const DiagnosticItem clamd = item(input, QStringLiteral("clamd"));
    QCOMPARE(int(clamd.level), level);
    QCOMPARE(bool(clamd.fix), fixable);
    if (fixable) {
        QCOMPARE(*clamd.fix, PrivilegedHelper::Action::ClamdStart);
        QVERIFY(clamd.command.contains(QLatin1String("clamav-daemon.service")));
    }
    // clamd injoignable : pas de signatures à juger.
    QVERIFY(!has(input, QStringLiteral("signatures")));
}

void TestDiagnostics::fedoraExampleLine()
{
    DiagnosticInput input = clamdDown(ClamdClient::Error::SocketNotFound);
    input.config = ClamdConfig::parse(QStringLiteral("Example\n#LocalSocket /run/clamd.scan/clamd.sock\n"));
    input.config.path = QStringLiteral("/etc/clamd.d/scan.conf");
    input.clamdService = QStringLiteral("clamd@scan.service");
    input.clamdServiceState = QStringLiteral("failed");

    const DiagnosticItem clamd = item(input, QStringLiteral("clamd"));
    QCOMPARE(int(clamd.level), int(DiagnosticItem::Level::Error));
    QCOMPARE(*clamd.fix, PrivilegedHelper::Action::ClamdConfigure);
    QVERIFY(clamd.text.contains(QLatin1String("Example")));
    QVERIFY(clamd.command.contains(QLatin1String("/etc/clamd.d/scan.conf")));
    QVERIFY(clamd.command.contains(QLatin1String("clamd@scan.service")));
}

void TestDiagnostics::fedoraSocketDisabled()
{
    DiagnosticInput input = clamdDown(ClamdClient::Error::SocketNotFound);
    input.config = ClamdConfig::parse(QStringLiteral("#LocalSocket /run/clamd.scan/clamd.sock\n"));
    input.config.path = QStringLiteral("/etc/clamd.d/scan.conf");
    input.clamdService = QStringLiteral("clamd@scan.service");

    const DiagnosticItem clamd = item(input, QStringLiteral("clamd"));
    QCOMPARE(*clamd.fix, PrivilegedHelper::Action::ClamdConfigure);
    QVERIFY(clamd.text.contains(QLatin1String("LocalSocket")));
}

void TestDiagnostics::clamdNotInstalled()
{
    DiagnosticInput input = clamdDown(ClamdClient::Error::SocketNotFound);
    input.config = {};
    input.clamdService.clear();
    input.distribution = {QStringLiteral("fedora")};
    const DiagnosticItem clamd = item(input, QStringLiteral("clamd"));
    QCOMPARE(int(clamd.level), int(DiagnosticItem::Level::Error));
    QVERIFY(!clamd.fix);
    QCOMPARE(clamd.command, QStringLiteral("sudo dnf install clamd clamav-update"));
    // Sans configuration, rien à dire sur les limites.
    QVERIFY(!has(input, QStringLiteral("limits")));
}

void TestDiagnostics::permissionDenied()
{
    DiagnosticInput input = clamdDown(ClamdClient::Error::PermissionDenied);
    input.socketGroup = QStringLiteral("virusgroup");
    input.userInSocketGroup = false;
    input.sessionHasSocketGroup = false;
    const DiagnosticItem clamd = item(input, QStringLiteral("clamd"));
    QCOMPARE(int(clamd.level), int(DiagnosticItem::Level::Error));
    QCOMPARE(*clamd.fix, PrivilegedHelper::Action::SocketGroupAdd);
    QCOMPARE(clamd.fixArgument, QStringLiteral("virusgroup"));
    QCOMPARE(clamd.command, QStringLiteral("sudo usermod -aG virusgroup alex"));
}

void TestDiagnostics::sessionToReopen()
{
    // Déjà ajouté au groupe, mais la session date d'avant : rien à corriger, juste se reconnecter.
    DiagnosticInput input = clamdDown(ClamdClient::Error::PermissionDenied);
    input.userInSocketGroup = true;
    input.sessionHasSocketGroup = false;
    const DiagnosticItem clamd = item(input, QStringLiteral("clamd"));
    QCOMPARE(int(clamd.level), int(DiagnosticItem::Level::Warning));
    QVERIFY(!clamd.fix);
    QVERIFY(clamd.command.isEmpty());
    QVERIFY(clamd.text.contains(QLatin1String("rouvrez")));
}

void TestDiagnostics::signatures()
{
    DiagnosticInput input = healthy();
    input.signaturesMaxAge = 3;
    input.version.signaturesDate = QDateTime::currentDateTime().addDays(-10);
    input.freshclamActive = false;
    DiagnosticItem signatures = item(input, QStringLiteral("signatures"));
    QCOMPARE(int(signatures.level), int(DiagnosticItem::Level::Warning));
    QCOMPARE(signatures.title, QStringLiteral("Signatures obsolètes (10 jours)"));
    QCOMPARE(*signatures.fix, PrivilegedHelper::Action::FreshclamEnable);

    // freshclam tourne mais échoue : son journal, pas de correction automatique.
    input.freshclamActive = true;
    signatures = item(input, QStringLiteral("signatures"));
    QVERIFY(!signatures.fix);
    QVERIFY(signatures.command.contains(QLatin1String("journalctl")));

    // freshclam absent : commande d'installation.
    input.freshclamInstalled = false;
    input.distribution = {QStringLiteral("fedora")};
    QCOMPARE(item(input, QStringLiteral("signatures")).command, QStringLiteral("sudo dnf install clamav-update"));

    // 0 : jamais obsolètes.
    input.signaturesMaxAge = 0;
    QCOMPARE(int(item(input, QStringLiteral("signatures")).level), int(DiagnosticItem::Level::Ok));

    // Aucune base chargée.
    input.version.signatures.clear();
    QCOMPARE(int(item(input, QStringLiteral("signatures")).level), int(DiagnosticItem::Level::Error));
}

void TestDiagnostics::selinux()
{
    DiagnosticInput input = healthy();
    input.selinuxEnforcing = true;
    input.antivirusCanScanSystem = false;
    const DiagnosticItem selinux = item(input, QStringLiteral("selinux"));
    QCOMPARE(int(selinux.level), int(DiagnosticItem::Level::Warning));
    QCOMPARE(*selinux.fix, PrivilegedHelper::Action::SelinuxAllowScan);
    QCOMPARE(selinux.command, QStringLiteral("sudo setsebool -P antivirus_can_scan_system 1"));

    input.antivirusCanScanSystem = true;
    QCOMPARE(int(item(input, QStringLiteral("selinux")).level), int(DiagnosticItem::Level::Ok));
    // SELinux permissif, ou booléen absent : rien à vérifier.
    input.selinuxEnforcing = false;
    QVERIFY(!has(input, QStringLiteral("selinux")));
    input.selinuxEnforcing = true;
    input.antivirusCanScanSystem.reset();
    QVERIFY(!has(input, QStringLiteral("selinux")));
}

void TestDiagnostics::alertExceedsMax()
{
    DiagnosticInput input = healthy();
    input.config.alertExceedsMax = false;
    const DiagnosticItem limits = item(input, QStringLiteral("limits"));
    QCOMPARE(int(limits.level), int(DiagnosticItem::Level::Info));
    QCOMPARE(*limits.fix, PrivilegedHelper::Action::ClamdAlertExceedsMax);
    QVERIFY(limits.command.contains(QLatin1String("/etc/clamav/clamd.conf")));
    QVERIFY(limits.command.contains(QLatin1String("clamav-daemon.service")));
    QVERIFY(limits.text.contains(QLatin1String("400 Mo")));
}

void TestDiagnostics::onAccess()
{
    DiagnosticInput input = healthy();
    input.onAccessState = OnAccessController::State::Inactive;
    DiagnosticItem onAccess = item(input, QStringLiteral("onaccess"));
    QCOMPARE(int(onAccess.level), int(DiagnosticItem::Level::Info));
    QCOMPARE(*onAccess.fix, PrivilegedHelper::Action::OnAccessEnable);

    input.onAccessState = OnAccessController::State::Failed;
    input.onAccessMessage = QStringLiteral("clamonacc ne peut pas joindre clamd");
    onAccess = item(input, QStringLiteral("onaccess"));
    QCOMPARE(int(onAccess.level), int(DiagnosticItem::Level::Error));
    QCOMPARE(onAccess.text, input.onAccessMessage);

    input.onAccessState = OnAccessController::State::ServiceMissing;
    QVERIFY(!item(input, QStringLiteral("onaccess")).fix);
    // Pas de service : la limite inotify ne le concerne pas.
    QVERIFY(!has(input, QStringLiteral("inotify")));
}

void TestDiagnostics::inotify()
{
    DiagnosticInput input = healthy();
    input.inotifyMaxWatches = 65536;
    DiagnosticItem inotify = item(input, QStringLiteral("inotify"));
    QCOMPARE(int(inotify.level), int(DiagnosticItem::Level::Info));
    QCOMPARE(*inotify.fix, PrivilegedHelper::Action::InotifyRaise);

    // Limite atteinte : clamonacc s'est arrêté.
    input.inotifyMaxWatches = 1048576;
    input.inotifyLimitReached = true;
    inotify = item(input, QStringLiteral("inotify"));
    QCOMPARE(int(inotify.level), int(DiagnosticItem::Level::Error));
    QVERIFY(inotify.fix);

    input.inotifyMaxWatches = -1;
    QVERIFY(!has(input, QStringLiteral("inotify")));
}

void TestDiagnostics::readsSystemFiles()
{
    QTemporaryDir root;
    bool enforcing = true;
    std::optional<bool> boolean = true;
    SystemDiagnostics::readSelinux(root.path(), &enforcing, &boolean);
    QVERIFY(!enforcing);
    QVERIFY(!boolean);
    QCOMPARE(SystemDiagnostics::readInotifyMaxWatches(root.path()), qint64(-1));

    writeFile(root.filePath(QStringLiteral("sys/fs/selinux/enforce")), "1");
    writeFile(root.filePath(QStringLiteral("sys/fs/selinux/booleans/antivirus_can_scan_system")), "0 0");
    writeFile(root.filePath(QStringLiteral("proc/sys/fs/inotify/max_user_watches")), "524288\n");
    SystemDiagnostics::readSelinux(root.path(), &enforcing, &boolean);
    QVERIFY(enforcing);
    QVERIFY(boolean && !*boolean);
    QCOMPARE(SystemDiagnostics::readInotifyMaxWatches(root.path()), qint64(524288));

    writeFile(root.filePath(QStringLiteral("sys/fs/selinux/booleans/antivirus_can_scan_system")), "1 1");
    SystemDiagnostics::readSelinux(root.path(), &enforcing, &boolean);
    QVERIFY(boolean && *boolean);
}

void TestDiagnostics::helperArguments()
{
    using Action = PrivilegedHelper::Action;
    QCOMPARE(PrivilegedHelper::helperArguments(Action::OnAccessEnable), QStringList{QStringLiteral("onaccess-enable")});
    QCOMPARE(PrivilegedHelper::helperArguments(Action::ClamdAlertExceedsMax),
             QStringList{QStringLiteral("clamd-alert-exceeds-max")});
    QCOMPARE(PrivilegedHelper::helperArguments(Action::SocketGroupAdd, QStringLiteral("clamav")),
             (QStringList{QStringLiteral("socket-group-add"), QStringLiteral("clamav")}));
}

void TestDiagnostics::helperExitCodes()
{
    QString message;
    QCOMPARE(PrivilegedHelper::resultFromExit(0, QString(), &message), PrivilegedHelper::Result::Success);
    QVERIFY(message.isEmpty());
    // Codes de pkexec.
    QCOMPARE(PrivilegedHelper::resultFromExit(126, QString(), &message), PrivilegedHelper::Result::Cancelled);
    QVERIFY(!message.isEmpty());
    QCOMPARE(PrivilegedHelper::resultFromExit(127, QString(), &message), PrivilegedHelper::Result::Failed);
    // Échec du programme d'aide : son message tel quel.
    QCOMPARE(PrivilegedHelper::resultFromExit(3, QStringLiteral("Aucun service clamd n'est installé.\n"), &message),
             PrivilegedHelper::Result::Failed);
    QCOMPARE(message, QStringLiteral("Aucun service clamd n'est installé."));
    QCOMPARE(PrivilegedHelper::resultFromExit(5, QString(), &message), PrivilegedHelper::Result::Failed);
    QVERIFY(message.contains(QLatin1String("5")));
}

void TestDiagnostics::helperRunsCommand()
{
    // pkexec remplacé par sh : le « programme d'aide » affiche ses arguments et échoue.
    QTemporaryDir dir;
    const QString script = dir.filePath(QStringLiteral("helper.sh"));
    writeFile(script, "echo \"reçu : $*\" >&2\nexit 3\n");

    PrivilegedHelper helper;
    helper.setCommand(QStringLiteral("/bin/sh"), script);
    QSignalSpy finished(&helper, &PrivilegedHelper::finished);
    helper.run(PrivilegedHelper::Action::SocketGroupAdd, QStringLiteral("clamav"));
    QVERIFY(helper.isRunning());
    helper.run(PrivilegedHelper::Action::OnAccessEnable); // ignoré : une action à la fois
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(0).value<PrivilegedHelper::Action>(), PrivilegedHelper::Action::SocketGroupAdd);
    QCOMPARE(finished.at(0).at(1).value<PrivilegedHelper::Result>(), PrivilegedHelper::Result::Failed);
    QCOMPARE(finished.at(0).at(2).toString(), QStringLiteral("reçu : socket-group-add clamav"));
    QVERIFY(!helper.isRunning());
}

QTEST_GUILESS_MAIN(TestDiagnostics)
#include "tst_diagnostics.moc"
