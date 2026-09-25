#include "core/ThreatText.h"
#include "system/DesktopNotifier.h"

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QSignalSpy>
#include <QTest>

// Faux serveur de notifications (org.freedesktop.Notifications) : enregistre
// les appels et envoie les signaux à la demande du test.
class FakeNotifications : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")

public:
    struct Call
    {
        QString appName;
        uint replacesId = 0;
        QString icon;
        QString title;
        QString body;
        QStringList actions;
        QVariantMap hints;
        int timeout = 0;
    };

    QList<Call> calls;
    QList<uint> closedIds;
    QStringList capabilities{QStringLiteral("actions"), QStringLiteral("body"), QStringLiteral("body-markup")};
    bool refuse = false;
    uint nextId = 1;

public slots:
    uint Notify(const QString &appName, uint replacesId, const QString &icon, const QString &title,
                const QString &body, const QStringList &actions, const QVariantMap &hints, int timeout)
    {
        if (refuse) {
            sendErrorReply(QDBusError::Failed, QStringLiteral("refusé"));
            return 0;
        }
        calls.append({appName, replacesId, icon, title, body, actions, hints, timeout});
        return replacesId != 0 ? replacesId : nextId++;
    }
    QStringList GetCapabilities() { return capabilities; }
    void CloseNotification(uint id) { closedIds.append(id); }

signals:
    void ActionInvoked(uint id, const QString &action);
    void NotificationClosed(uint id, uint reason);
    void ActivationToken(uint id, const QString &token);
};

class TestNotifications : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Textes
    void describe_data();
    void describe();
    void shortPath_data();
    void shortPath();
    void kind_data();
    void kind();
    void realtimeAlertSingle();
    void realtimeAlertGroup();
    void realtimeAlertSuspicious();
    void scanAlert();

    // Notifications D-Bus (faux serveur sur un bus de session privé)
    void sendsHintsAndActions();
    void escapesBodyOnlyForMarkupServers();
    void updateReplacesDisplayedNotification();
    void updateWhileFirstReplyPending();
    void actionCarriesActivationToken();
    void closedByUserOrByCall();
    void unavailableWithoutService();
    void refusedNotificationReported();

private:
    void requireBus();
    std::unique_ptr<DesktopNotifier> notifier();
    static DesktopNotifier::Notification alert(const QString &body = QStringLiteral("texte"));

    QDBusConnection m_fakeBus{QString()};
    FakeNotifications *m_server = nullptr;
};

void TestNotifications::init()
{
    if (!QDBusConnection::sessionBus().isConnected())
        return;
    m_fakeBus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("fake-notifications"));
    m_server = new FakeNotifications;
    QVERIFY(m_fakeBus.registerObject(QStringLiteral("/org/freedesktop/Notifications"), m_server,
                                     QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals));
    QVERIFY(m_fakeBus.registerService(QString::fromLatin1(DesktopNotifier::kService)));
}

void TestNotifications::cleanup()
{
    if (!m_server)
        return;
    m_fakeBus.unregisterService(QString::fromLatin1(DesktopNotifier::kService));
    m_fakeBus.unregisterObject(QStringLiteral("/org/freedesktop/Notifications"));
    delete m_server;
    m_server = nullptr;
    QDBusConnection::disconnectFromBus(QStringLiteral("fake-notifications"));
}

void TestNotifications::requireBus()
{
    if (!m_server)
        QSKIP("Pas de bus de session D-Bus : lancer le test avec dbus-run-session.");
}

std::unique_ptr<DesktopNotifier> TestNotifications::notifier()
{
    auto result = std::make_unique<DesktopNotifier>(QStringLiteral("Linux Defender"), QStringLiteral("linux-defender"));
    QTest::qWait(100); // réponse à GetCapabilities
    return result;
}

DesktopNotifier::Notification TestNotifications::alert(const QString &body)
{
    DesktopNotifier::Notification notification;
    notification.title = QStringLiteral("Menace détectée : eicar.com");
    notification.body = body;
    notification.icon = QStringLiteral("security-low");
    notification.urgency = DesktopNotifier::Urgency::Critical;
    notification.timeoutMsecs = 0;
    notification.actions = {{QStringLiteral("default"), QStringLiteral("Afficher les détails")},
                            {QStringLiteral("folder"), QStringLiteral("Ouvrir le dossier")}};
    return notification;
}

// --- Textes ------------------------------------------------------------------

void TestNotifications::describe_data()
{
    QTest::addColumn<QString>("signature");
    QTest::addColumn<QString>("expected");

    QTest::newRow("EICAR") << "Win.Test.EICAR_HDB-1" << "Fichier de test EICAR (inoffensif)";
    QTest::newRow("EICAR, base tierce") << "Win.Test.EICAR_HDB-1.UNOFFICIAL" << "Fichier de test EICAR (inoffensif)";
    QTest::newRow("EICAR, ancien nom") << "Eicar-Signature" << "Fichier de test EICAR (inoffensif)";
    QTest::newRow("cheval de Troie") << "Win.Trojan.Agent-1234567-0" << "Cheval de Troie (Windows)";
    QTest::newRow("Linux") << "Unix.Malware.Mirai-9876543-0" << "Logiciel malveillant (Linux/Unix)";
    QTest::newRow("rançongiciel") << "Win.Ransomware.WannaCry-6313787-0" << "Rançongiciel (Windows)";
    QTest::newRow("document Word") << "Doc.Downloader.Emotet-7580152-0" << "Téléchargeur malveillant (document Word)";
    QTest::newRow("PUA") << "PUA.Win.Adware.Agent-123-0" << "Programme potentiellement indésirable (Windows)";
    QTest::newRow("plateforme inconnue") << "Zzz.Trojan.Foo-1" << "Cheval de Troie";
    QTest::newRow("autre test") << "Clamav.Test.File-6" << "Fichier de test (inoffensif)";
    QTest::newRow("hameçonnage heuristique") << "Heuristics.Phishing.Email.SpoofedDomain"
                                              << "Hameçonnage probable (détection heuristique)";
    QTest::newRow("chiffré") << "Heuristics.Encrypted.Zip" << "Fichier chiffré, impossible à analyser";
    QTest::newRow("catégorie inconnue") << "Win.Countermeasure.Foo-1" << "Win.Countermeasure.Foo-1";
    QTest::newRow("nom libre") << "LinuxDefender.Test" << "LinuxDefender.Test";
}

void TestNotifications::describe()
{
    QFETCH(QString, signature);
    QFETCH(QString, expected);
    QCOMPARE(ThreatText::describe(signature), expected);
}

void TestNotifications::shortPath_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<QString>("expected");

    QTest::newRow("dossier personnel") << "/home/alex/Téléchargements" << "~/Téléchargements";
    QTest::newRow("dossier personnel lui-même") << "/home/alex" << "~";
    QTest::newRow("autre utilisateur") << "/home/alexandre/x" << "/home/alexandre/x";
    QTest::newRow("hors du dossier personnel") << "/run/media/alex/CLE" << "/run/media/alex/CLE";
    QTest::newRow("long : début et fin gardés")
        << "/home/alex/.var/app/app.zen_browser.zen/cache/zen/5alpemjo.Default (release)/safebrowsing-backup"
        << "~/.var/…/safebrowsing-backup";
    QTest::newRow("dernier dossier trop long")
        << "/home/alex/Documents/un-nom-de-dossier-vraiment-beaucoup-trop-long-pour-tenir-dans-la-notification"
        << "~/Documents/un-nom-de-dos…nir-dans-la-notification";
}

void TestNotifications::shortPath()
{
    QFETCH(QString, path);
    QFETCH(QString, expected);
    const QString result = ThreatText::shortPath(path, QStringLiteral("/home/alex"));
    QCOMPARE(result, expected);
    QVERIFY(result.size() <= 50);
}

void TestNotifications::kind_data()
{
    QTest::addColumn<QString>("signature");
    QTest::addColumn<int>("kind");

    const int threat = int(ThreatText::Kind::Threat);
    const int suspicious = int(ThreatText::Kind::Suspicious);
    const int unscanned = int(ThreatText::Kind::Unscanned);
    QTest::newRow("cheval de Troie") << "Win.Trojan.Agent-123-0" << threat;
    QTest::newRow("EICAR") << "Win.Test.EICAR_HDB-1" << threat;
    QTest::newRow("base tierce") << "Sanesecurity.Foxhole.Zip_fs220.UNOFFICIAL" << threat;
    QTest::newRow("PUA") << "PUA.Win.Adware.Agent-123-0" << suspicious;
    QTest::newRow("hameçonnage") << "Heuristics.Phishing.Email.SpoofedDomain" << suspicious;
    QTest::newRow("exécutable malformé") << "Heuristics.Broken.Executable" << suspicious;
    QTest::newRow("macros") << "Heuristics.OLE2.ContainsMacros" << suspicious;
    QTest::newRow("chiffré") << "Heuristics.Encrypted.Zip" << unscanned;
    QTest::newRow("chiffré (PDF)") << "Heuristics.Encrypted.PDF" << unscanned;
    QTest::newRow("MaxFileSize") << "Heuristics.Limits.Exceeded.MaxFileSize" << unscanned;
    QTest::newRow("MaxScanSize") << "Heuristics.Limits.Exceeded.MaxScanSize" << unscanned;
}

void TestNotifications::kind()
{
    QFETCH(QString, signature);
    QFETCH(int, kind);
    QCOMPARE(int(ThreatText::kind(signature)), kind);
}

void TestNotifications::realtimeAlertSingle()
{
    const ThreatText::Alert alert = ThreatText::realtimeAlert(
        {{QStringLiteral("/home/alex/Téléchargements/eicar.com"), QStringLiteral("Win.Test.EICAR_HDB-1")}},
        QStringLiteral("/home/alex"));
    QCOMPARE(alert.title, QStringLiteral("Menace détectée : eicar.com"));
    QCOMPARE(alert.body, QStringLiteral("Fichier de test EICAR (inoffensif)\nDans ~/Téléchargements, toujours en place."));
}

void TestNotifications::realtimeAlertGroup()
{
    QList<ThreatText::Threat> threats;
    for (int i = 1; i <= 5; ++i)
        threats.append({QStringLiteral("/home/alex/archive/f%1.exe").arg(i), QStringLiteral("Win.Trojan.Agent-1-0")});
    const ThreatText::Alert alert = ThreatText::realtimeAlert(threats, QStringLiteral("/home/alex"));
    QCOMPARE(alert.title, QStringLiteral("5 menaces détectées"));
    QCOMPARE(alert.body, QStringLiteral("f1.exe : Cheval de Troie (Windows)\n"
                                        "f2.exe : Cheval de Troie (Windows)\n"
                                        "f3.exe : Cheval de Troie (Windows)\n"
                                        "… et 2 autres\n"
                                        "Aucun fichier n'a été supprimé ni déplacé."));

    threats = threats.mid(0, 2);
    QCOMPARE(ThreatText::realtimeAlert(threats, QStringLiteral("/home/alex")).title, QStringLiteral("2 menaces détectées"));
    QVERIFY(!ThreatText::realtimeAlert(threats, QStringLiteral("/home/alex")).body.contains(QStringLiteral("autre")));
}

void TestNotifications::realtimeAlertSuspicious()
{
    const QString home = QStringLiteral("/home/alex");
    const ThreatText::Threat pua{QStringLiteral("/home/alex/Téléchargements/outil.exe"),
                                 QStringLiteral("PUA.Win.Tool.Agent-1-0")};
    const ThreatText::Threat trojan{QStringLiteral("/home/alex/Téléchargements/f.exe"),
                                    QStringLiteral("Win.Trojan.Agent-1-0")};
    ThreatText::Alert alert = ThreatText::realtimeAlert({pua}, home);
    QCOMPARE(alert.title, QStringLiteral("Fichier suspect : outil.exe"));
    QCOMPARE(alert.body, QStringLiteral("Programme potentiellement indésirable (Windows)\n"
                                        "Dans ~/Téléchargements, toujours en place."));
    QCOMPARE(ThreatText::realtimeAlert({pua, pua}, home).title, QStringLiteral("2 fichiers suspects détectés"));
    // Une seule vraie menace suffit : c'est une alerte de menace.
    QCOMPARE(ThreatText::realtimeAlert({pua, trojan}, home).title, QStringLiteral("2 menaces détectées"));

    alert = ThreatText::suspiciousAlert({pua}, 1, QStringLiteral("Analyse terminée : 3 fichiers analysés."));
    QCOMPARE(alert.title, QStringLiteral("1 fichier suspect trouvé par l'analyse"));
    QCOMPARE(alert.body, QStringLiteral("Analyse terminée : 3 fichiers analysés.\n"
                                        "outil.exe : Programme potentiellement indésirable (Windows)"));
    QCOMPARE(ThreatText::suspiciousAlert({pua}, 4, QString()).title,
             QStringLiteral("4 fichiers suspects trouvés par l'analyse"));
}

void TestNotifications::scanAlert()
{
    const ThreatText::Alert alert = ThreatText::scanAlert(
        {{QStringLiteral("/run/media/alex/CLE/setup.exe"), QStringLiteral("Win.Trojan.Agent-1-0")}}, 2,
        QStringLiteral("Scan terminé : 120 fichiers analysés, 2 menaces détectées."));
    QCOMPARE(alert.title, QStringLiteral("2 menaces détectées par le scan"));
    QCOMPARE(alert.body, QStringLiteral("Scan terminé : 120 fichiers analysés, 2 menaces détectées.\n"
                                        "setup.exe : Cheval de Troie (Windows)\n"
                                        "… et 1 autre"));
    QCOMPARE(ThreatText::scanAlert({}, 1, QString()).title, QStringLiteral("1 menace détectée par le scan"));
}

// --- Notifications D-Bus -----------------------------------------------------

void TestNotifications::sendsHintsAndActions()
{
    requireBus();
    auto desktop = notifier();
    QVERIFY(desktop->show(QStringLiteral("realtime"), alert()));
    QTRY_COMPARE_WITH_TIMEOUT(m_server->calls.size(), 1, 3000);

    const FakeNotifications::Call &call = m_server->calls.first();
    QCOMPARE(call.appName, QStringLiteral("Linux Defender"));
    QCOMPARE(call.replacesId, 0u);
    QCOMPARE(call.icon, QStringLiteral("security-low"));
    QCOMPARE(call.title, QStringLiteral("Menace détectée : eicar.com"));
    QCOMPARE(call.actions, (QStringList{QStringLiteral("default"), QStringLiteral("Afficher les détails"),
                                        QStringLiteral("folder"), QStringLiteral("Ouvrir le dossier")}));
    QCOMPARE(call.timeout, 0);
    // Urgence : un octet (« y ») selon la spécification, 2 = critique.
    QCOMPARE(call.hints.value(QStringLiteral("urgency")).metaType(), QMetaType::fromType<uchar>());
    QCOMPARE(call.hints.value(QStringLiteral("urgency")).value<uchar>(), uchar(2));
    QCOMPARE(call.hints.value(QStringLiteral("desktop-entry")).toString(), QStringLiteral("linux-defender"));
}

void TestNotifications::escapesBodyOnlyForMarkupServers()
{
    requireBus();
    const QString body = QStringLiteral("<b>facture & co</b>.exe");
    {
        auto desktop = notifier();
        desktop->show(QStringLiteral("k"), alert(body));
        QTRY_COMPARE_WITH_TIMEOUT(m_server->calls.size(), 1, 3000);
        QCOMPARE(m_server->calls.last().body, QStringLiteral("&lt;b&gt;facture &amp; co&lt;/b&gt;.exe"));
    }
    m_server->capabilities = {QStringLiteral("actions"), QStringLiteral("body")};
    auto desktop = notifier();
    desktop->show(QStringLiteral("k"), alert(body));
    QTRY_COMPARE_WITH_TIMEOUT(m_server->calls.size(), 2, 3000);
    QCOMPARE(m_server->calls.last().body, body);
}

void TestNotifications::updateReplacesDisplayedNotification()
{
    requireBus();
    auto desktop = notifier();
    desktop->show(QStringLiteral("realtime"), alert());
    QTRY_COMPARE_WITH_TIMEOUT(m_server->calls.size(), 1, 3000);
    QTest::qWait(100);
    desktop->show(QStringLiteral("realtime"), alert(QStringLiteral("2 menaces")));
    desktop->show(QStringLiteral("scan"), alert(QStringLiteral("autre clé")));
    QTRY_COMPARE_WITH_TIMEOUT(m_server->calls.size(), 3, 3000);
    QCOMPARE(m_server->calls[1].replacesId, 1u); // même clé : remplace
    QCOMPARE(m_server->calls[2].replacesId, 0u); // autre clé : nouvelle notification
}

void TestNotifications::updateWhileFirstReplyPending()
{
    requireBus();
    // Deux détections coup sur coup : la seconde attend l'identifiant de la
    // première pour la remplacer, au lieu d'afficher une seconde notification.
    auto desktop = notifier();
    desktop->show(QStringLiteral("realtime"), alert(QStringLiteral("1")));
    desktop->show(QStringLiteral("realtime"), alert(QStringLiteral("2")));
    desktop->show(QStringLiteral("realtime"), alert(QStringLiteral("3")));
    QTRY_COMPARE_WITH_TIMEOUT(m_server->calls.size(), 2, 3000);
    QTest::qWait(200);
    QCOMPARE(m_server->calls.size(), 2); // la version « 2 », dépassée, n'est jamais envoyée
    QCOMPARE(m_server->calls[1].replacesId, 1u);
    QCOMPARE(m_server->calls[1].body, QStringLiteral("3"));
}

void TestNotifications::actionCarriesActivationToken()
{
    requireBus();
    auto desktop = notifier();
    QSignalSpy actions(desktop.get(), &DesktopNotifier::actionInvoked);
    desktop->show(QStringLiteral("realtime"), alert());
    QTRY_COMPARE_WITH_TIMEOUT(m_server->calls.size(), 1, 3000);
    QTest::qWait(100);

    emit m_server->ActionInvoked(42, QStringLiteral("folder")); // notification d'une autre application
    emit m_server->ActivationToken(1, QStringLiteral("jeton"));
    emit m_server->ActionInvoked(1, QStringLiteral("folder"));
    QTRY_COMPARE_WITH_TIMEOUT(actions.count(), 1, 3000);
    QCOMPARE(actions.first().at(0).toString(), QStringLiteral("realtime"));
    QCOMPARE(actions.first().at(1).toString(), QStringLiteral("folder"));
    QCOMPARE(actions.first().at(2).toString(), QStringLiteral("jeton"));
}

void TestNotifications::closedByUserOrByCall()
{
    requireBus();
    auto desktop = notifier();
    QSignalSpy closed(desktop.get(), &DesktopNotifier::closed);
    desktop->show(QStringLiteral("realtime"), alert());
    desktop->show(QStringLiteral("scan"), alert());
    QTRY_COMPARE_WITH_TIMEOUT(m_server->calls.size(), 2, 3000);
    QTest::qWait(100);

    // Fermée par l'utilisateur : signalée.
    emit m_server->NotificationClosed(1, 2);
    QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 3000);
    QCOMPARE(closed.first().first().toString(), QStringLiteral("realtime"));

    // Fermée par l'application : demandée au serveur, pas signalée en retour.
    desktop->close(QStringLiteral("scan"));
    QTRY_COMPARE_WITH_TIMEOUT(m_server->closedIds, QList<uint>{2}, 3000);
    emit m_server->NotificationClosed(2, 3);
    QTest::qWait(200);
    QCOMPARE(closed.count(), 1);

    // Une nouvelle alerte après fermeture est une nouvelle notification.
    desktop->show(QStringLiteral("realtime"), alert());
    QTRY_COMPARE_WITH_TIMEOUT(m_server->calls.size(), 3, 3000);
    QCOMPARE(m_server->calls.last().replacesId, 0u);
}

void TestNotifications::unavailableWithoutService()
{
    requireBus();
    QVERIFY(m_fakeBus.unregisterService(QString::fromLatin1(DesktopNotifier::kService)));
    DesktopNotifier desktop(QStringLiteral("Linux Defender"), QStringLiteral("linux-defender"));
    QVERIFY(!desktop.show(QStringLiteral("realtime"), alert())); // l'appelant se rabat sur showMessage()
}

void TestNotifications::refusedNotificationReported()
{
    requireBus();
    m_server->refuse = true;
    auto desktop = notifier();
    QSignalSpy failed(desktop.get(), &DesktopNotifier::failed);
    QVERIFY(desktop->show(QStringLiteral("realtime"), alert()));
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
    QCOMPARE(failed.first().first().toString(), QStringLiteral("realtime"));
    QCOMPARE(failed.first().at(1).value<DesktopNotifier::Notification>().title,
             QStringLiteral("Menace détectée : eicar.com"));
}

QTEST_GUILESS_MAIN(TestNotifications)
#include "tst_notifications.moc"
