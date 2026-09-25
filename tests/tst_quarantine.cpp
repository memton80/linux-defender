#include "FakeClamd.h"
#include "core/Quarantine.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include <sys/stat.h>
#include <unistd.h>

namespace
{
void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool isRoot()
{
    return ::geteuid() == 0;
}
}

class TestQuarantine : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void addsAndRestores();
    void restoreNeverOverwrites();
    void refusesSymlinks();
    void refusesUnwritableFolder();
    void detectsDamagedFile();
    void removesForGood();
    void asynchronousOperations();

private:
    QString quarantineDir() const { return m_dir->filePath(QStringLiteral("quarantaine")); }
    QString filesDir() const { return m_dir->filePath(QStringLiteral("fichiers")); }

    std::unique_ptr<QTemporaryDir> m_dir;
};

void TestQuarantine::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
}

void TestQuarantine::addsAndRestores()
{
    const QString path = filesDir() + QStringLiteral("/facture.pdf.exe");
    const QByteArray content = QByteArray("MZ") + FakeClamd::kVirusMarker + QByteArray(3 * 1024 * 1024, 'x');
    writeFile(path, content);
    QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                                            | QFileDevice::ReadGroup | QFileDevice::ExeGroup));

    QuarantineEntry entry;
    QString error;
    QVERIFY2(Quarantine::addFile(quarantineDir(), {path, QStringLiteral("Win.Trojan.Agent-1")}, &entry, &error),
             qPrintable(error));
    QVERIFY(error.isEmpty());
    QVERIFY(!QFile::exists(path)); // retiré de son emplacement

    // Dossier privé ; fichier brouillé : plus rien de reconnaissable, ni exécutable.
    QCOMPARE(QFileInfo(quarantineDir()).permissions() & 0x0077, QFileDevice::Permissions());
    const QString data = QDir(quarantineDir()).filePath(entry.id + QStringLiteral(".bin"));
    QVERIFY(!readFile(data).contains(FakeClamd::kVirusMarker));
    QVERIFY(!readFile(data).contains("MZ"));
    QVERIFY(!QFileInfo(data).isExecutable());

    QCOMPARE(entry.originalPath, path);
    QCOMPARE(entry.threat, QStringLiteral("Win.Trojan.Agent-1"));
    QCOMPARE(entry.size, qint64(content.size()));
    QCOMPARE(entry.sha256, QString::fromLatin1(QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex()));
    QCOMPARE(entry.permissions, 0750u);
    const QList<QuarantineEntry> entries = Quarantine::readEntries(quarantineDir());
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().id, entry.id);
    QCOMPARE(entries.first().sha256, entry.sha256);

    // Restauration : même contenu, mêmes droits, quarantaine vide.
    QString restored;
    QVERIFY2(Quarantine::restoreFile(quarantineDir(), entry.id, &restored, &error), qPrintable(error));
    QCOMPARE(restored, path);
    QCOMPARE(readFile(path), content);
    struct stat info;
    QCOMPARE(::stat(QFile::encodeName(path).constData(), &info), 0);
    QCOMPARE(uint(info.st_mode & 0777), 0750u);
    QVERIFY(Quarantine::readEntries(quarantineDir()).isEmpty());
    QVERIFY(!QFile::exists(data));
}

void TestQuarantine::restoreNeverOverwrites()
{
    const QString path = filesDir() + QStringLiteral("/archive.tar.gz");
    const QString hidden = filesDir() + QStringLiteral("/.bashrc");
    QuarantineEntry first;
    QuarantineEntry second;
    QuarantineEntry third;
    QString error;
    writeFile(path, "premier");
    QVERIFY(Quarantine::addFile(quarantineDir(), {path, QStringLiteral("x")}, &first, &error));
    writeFile(path, "second");
    QVERIFY(Quarantine::addFile(quarantineDir(), {path, QStringLiteral("x")}, &second, &error));
    writeFile(hidden, "alias");
    QVERIFY(Quarantine::addFile(quarantineDir(), {hidden, QStringLiteral("x")}, &third, &error));

    writeFile(path, "nouveau fichier, créé depuis");
    QString restored;
    QVERIFY(Quarantine::restoreFile(quarantineDir(), first.id, &restored, &error));
    QCOMPARE(restored, filesDir() + QStringLiteral("/archive (restauré).tar.gz"));
    QCOMPARE(readFile(restored), QByteArray("premier"));
    QVERIFY(Quarantine::restoreFile(quarantineDir(), second.id, &restored, &error));
    QCOMPARE(restored, filesDir() + QStringLiteral("/archive (restauré 2).tar.gz"));
    QCOMPARE(readFile(path), QByteArray("nouveau fichier, créé depuis")); // jamais écrasé

    writeFile(hidden, "autre");
    QVERIFY(Quarantine::restoreFile(quarantineDir(), third.id, &restored, &error));
    QCOMPARE(restored, filesDir() + QStringLiteral("/.bashrc (restauré)"));

    // Dossier d'origine supprimé entre-temps : recréé.
    const QString deep = filesDir() + QStringLiteral("/a/b/c.txt");
    writeFile(deep, "profond");
    QuarantineEntry deepEntry;
    QVERIFY(Quarantine::addFile(quarantineDir(), {deep, QStringLiteral("x")}, &deepEntry, &error));
    QVERIFY(QDir(filesDir() + QStringLiteral("/a")).removeRecursively());
    QVERIFY(Quarantine::restoreFile(quarantineDir(), deepEntry.id, &restored, &error));
    QCOMPARE(readFile(deep), QByteArray("profond"));
}

void TestQuarantine::refusesSymlinks()
{
    const QString target = filesDir() + QStringLiteral("/cible.txt");
    const QString link = filesDir() + QStringLiteral("/lien.txt");
    writeFile(target, "contenu");
    QVERIFY(QFile::link(target, link));

    QuarantineEntry entry;
    QString error;
    QVERIFY(!Quarantine::addFile(quarantineDir(), {link, QStringLiteral("x")}, &entry, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(QFileInfo(link).isSymLink());
    QCOMPARE(readFile(target), QByteArray("contenu"));
    QVERIFY(Quarantine::readEntries(quarantineDir()).isEmpty());
}

void TestQuarantine::refusesUnwritableFolder()
{
    if (isRoot())
        QSKIP("root ignore les permissions des dossiers");
    const QString path = filesDir() + QStringLiteral("/verrou/fichier.exe");
    writeFile(path, FakeClamd::kVirusMarker);
    const QString folder = QFileInfo(path).absolutePath();
    QVERIFY(QFile::setPermissions(folder, QFileDevice::ReadOwner | QFileDevice::ExeOwner));

    QuarantineEntry entry;
    QString error;
    QVERIFY(!Quarantine::addFile(quarantineDir(), {path, QStringLiteral("x")}, &entry, &error));
    QVERIFY(error.contains(folder));
    QVERIFY(QFile::exists(path)); // rien n'a bougé
    QVERIFY(Quarantine::readEntries(quarantineDir()).isEmpty());
    QFile::setPermissions(folder, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

void TestQuarantine::detectsDamagedFile()
{
    const QString path = filesDir() + QStringLiteral("/doc.txt");
    writeFile(path, "contenu d'origine");
    QuarantineEntry entry;
    QString error;
    QVERIFY(Quarantine::addFile(quarantineDir(), {path, QStringLiteral("x")}, &entry, &error));

    // Un octet modifié dans le fichier brouillé.
    QFile data(QDir(quarantineDir()).filePath(entry.id + QStringLiteral(".bin")));
    QVERIFY(data.open(QIODevice::ReadWrite));
    QVERIFY(data.seek(data.size() - 1));
    const char last = data.read(1).at(0);
    QVERIFY(data.seek(data.size() - 1));
    data.write(QByteArray(1, char(last ^ 0x01)));
    data.close();

    QString restored;
    QVERIFY(!Quarantine::restoreFile(quarantineDir(), entry.id, &restored, &error));
    QVERIFY2(error.contains(QStringLiteral("abîmé")), qPrintable(error));
    QVERIFY(!QFile::exists(path)); // rien de restauré à moitié
    QCOMPARE(Quarantine::readEntries(quarantineDir()).size(), 1); // toujours en quarantaine
}

void TestQuarantine::removesForGood()
{
    const QString path = filesDir() + QStringLiteral("/virus.exe");
    writeFile(path, FakeClamd::kVirusMarker);
    QuarantineEntry entry;
    QString error;
    QVERIFY(Quarantine::addFile(quarantineDir(), {path, QStringLiteral("x")}, &entry, &error));
    QVERIFY(Quarantine::removeFile(quarantineDir(), entry.id, &error));
    QVERIFY(Quarantine::readEntries(quarantineDir()).isEmpty());
    QVERIFY(QDir(quarantineDir()).entryList(QDir::Files).isEmpty());
    QVERIFY(!QFile::exists(path));
}

void TestQuarantine::asynchronousOperations()
{
    const QString first = filesDir() + QStringLiteral("/un.exe");
    const QString second = filesDir() + QStringLiteral("/deux.exe");
    writeFile(first, "1");
    writeFile(second, "2");

    Quarantine quarantine(quarantineDir());
    QVERIFY(quarantine.entries().isEmpty());
    QSignalSpy finished(&quarantine, &Quarantine::finished);
    quarantine.add({{first, QStringLiteral("Win.Trojan.A")},
                    {second, QStringLiteral("Win.Trojan.B")},
                    {filesDir() + QStringLiteral("/absent.exe"), QStringLiteral("x")}});
    QVERIFY(quarantine.isBusy());
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 3, 5000);
    QVERIFY(finished.at(0).at(2).toString().isEmpty());
    QVERIFY(finished.at(1).at(2).toString().isEmpty());
    QVERIFY(!finished.at(2).at(2).toString().isEmpty()); // fichier absent : erreur, les autres faits quand même
    QCOMPARE(quarantine.entries().size(), 2);
    QVERIFY(quarantine.isRecentlyQuarantined(first));
    QVERIFY(!quarantine.isRecentlyQuarantined(filesDir() + QStringLiteral("/absent.exe")));

    // Restauration puis suppression, dans l'ordre demandé.
    QString firstId;
    QString secondId;
    for (const QuarantineEntry &entry : quarantine.entries())
        (entry.originalPath == first ? firstId : secondId) = entry.id;
    quarantine.restore(firstId);
    quarantine.remove(secondId);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 5, 5000);
    QCOMPARE(finished.at(3).at(0).value<Quarantine::Operation>(), Quarantine::Operation::Restore);
    QCOMPARE(finished.at(3).at(1).toString(), first);
    QCOMPARE(finished.at(4).at(0).value<Quarantine::Operation>(), Quarantine::Operation::Remove);
    QVERIFY(quarantine.entries().isEmpty());
    QCOMPARE(readFile(first), QByteArray("1"));
    QVERIFY(!QFile::exists(second));
}

QTEST_GUILESS_MAIN(TestQuarantine)
#include "tst_quarantine.moc"
