#include "core/ScanHistory.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace
{
ScanRecord record(const QString &path, qint64 infected = 0)
{
    ScanSummary summary;
    summary.paths = {path};
    summary.started = QDateTime(QDate(2026, 9, 24), QTime(16, 2, 30, 125));
    summary.elapsedMsecs = 42000;
    summary.scanned = 116;
    summary.infected = infected;
    summary.errors = 21;
    summary.skipped = 3;
    for (qint64 i = 0; i < infected; ++i)
        summary.threats.append({path + QStringLiteral("/virus-%1.exe").arg(i), ScanResult::Status::Infected,
                                QStringLiteral("Win.Test.EICAR_HDB-1")});
    return ScanRecord::fromSummary(summary, ScanManager::Origin::Usb);
}
}

class TestScanHistory : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void startsEmpty();
    void savesAndReloads();
    void newestFirstAndTrimmed();
    void disabledKeepsNothing();
    void clears();
    void ignoresCorruptFile();

private:
    QString filePath() const { return m_dir->filePath(QStringLiteral("sous-dossier/history.json")); }

    std::unique_ptr<QTemporaryDir> m_dir;
};

void TestScanHistory::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
}

void TestScanHistory::startsEmpty()
{
    ScanHistory history(filePath());
    QVERIFY(history.records().isEmpty());
    QVERIFY(!history.last());
    QVERIFY(!QFile::exists(filePath()));
}

void TestScanHistory::savesAndReloads()
{
    {
        ScanHistory history(filePath());
        QSignalSpy changed(&history, &ScanHistory::changed);
        history.add(record(QStringLiteral("/run/media/alex/KRS 01"), 2));
        QCOMPARE(changed.count(), 1);
        QVERIFY(QFile::exists(filePath())); // dossier parent créé
    }

    ScanHistory reloaded(filePath());
    QCOMPARE(reloaded.records().size(), 1);
    const ScanRecord loaded = *reloaded.last();
    const ScanRecord expected = record(QStringLiteral("/run/media/alex/KRS 01"), 2);
    QCOMPARE(loaded.started, expected.started);
    QCOMPARE(loaded.elapsedMsecs, expected.elapsedMsecs);
    QCOMPARE(loaded.origin, ScanManager::Origin::Usb);
    QCOMPARE(loaded.paths, expected.paths);
    QCOMPARE(loaded.scanned, qint64(116));
    QCOMPARE(loaded.infected, qint64(2));
    QCOMPARE(loaded.errors, qint64(21));
    QCOMPARE(loaded.skipped, qint64(3));
    QCOMPARE(loaded.threats.size(), 2);
    QCOMPARE(loaded.threats.at(1).path, expected.threats.at(1).path);
    QCOMPARE(loaded.threats.at(1).detail, QStringLiteral("Win.Test.EICAR_HDB-1"));
    QCOMPARE(int(loaded.threats.at(1).status), int(ScanResult::Status::Infected));

    // Retour au bilan d'origine, pour les textes partagés avec les scans.
    const ScanSummary summary = loaded.toSummary();
    QCOMPARE(summary.infected, qint64(2));
    QCOMPARE(summary.paths, expected.paths);
}

void TestScanHistory::newestFirstAndTrimmed()
{
    ScanHistory history(filePath());
    history.setMaxRecords(2);
    history.add(record(QStringLiteral("/a")));
    history.add(record(QStringLiteral("/b")));
    history.add(record(QStringLiteral("/c")));

    QCOMPARE(history.records().size(), 2);
    QCOMPARE(history.records().at(0).paths, QStringList{QStringLiteral("/c")});
    QCOMPARE(history.records().at(1).paths, QStringList{QStringLiteral("/b")});
    QCOMPARE(ScanHistory(filePath()).records().size(), 2);

    // Réduire la limite oublie aussitôt les plus anciennes.
    QSignalSpy changed(&history, &ScanHistory::changed);
    history.setMaxRecords(1);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(history.records().size(), 1);
    QCOMPARE(history.last()->paths, QStringList{QStringLiteral("/c")});
    QCOMPARE(ScanHistory(filePath()).records().size(), 1);
}

void TestScanHistory::disabledKeepsNothing()
{
    ScanHistory history(filePath());
    history.add(record(QStringLiteral("/a")));
    history.setMaxRecords(0);
    QVERIFY(history.records().isEmpty());

    QSignalSpy changed(&history, &ScanHistory::changed);
    history.add(record(QStringLiteral("/b")));
    QCOMPARE(changed.count(), 0);
    QVERIFY(history.records().isEmpty());
    QVERIFY(ScanHistory(filePath()).records().isEmpty());
}

void TestScanHistory::clears()
{
    ScanHistory history(filePath());
    history.add(record(QStringLiteral("/a")));
    QSignalSpy changed(&history, &ScanHistory::changed);
    history.clear();
    QCOMPARE(changed.count(), 1);
    QVERIFY(history.records().isEmpty());
    QVERIFY(ScanHistory(filePath()).records().isEmpty());

    history.clear(); // déjà vide : aucun signal
    QCOMPARE(changed.count(), 1);
}

void TestScanHistory::ignoresCorruptFile()
{
    QVERIFY(QDir().mkpath(QFileInfo(filePath()).absolutePath()));
    QFile file(filePath());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{ pas du JSON");
    file.close();

    ScanHistory history(filePath());
    QVERIFY(history.records().isEmpty());
    // Et l'historique repart de zéro.
    history.add(record(QStringLiteral("/a")));
    QCOMPARE(ScanHistory(filePath()).records().size(), 1);
}

QTEST_GUILESS_MAIN(TestScanHistory)
#include "tst_scanhistory.moc"
