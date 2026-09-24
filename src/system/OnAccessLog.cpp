#include "OnAccessLog.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <sys/stat.h>

#include <algorithm>
#include <iterator>

namespace
{
constexpr qint64 kMaxHistoryBytes = 256 * 1024; // lecture de l'historique au démarrage
constexpr int kMaxHistoryEntries = 500;
constexpr int kDuplicateWindowSecs = 10;
constexpr int kTailSize = 64;

// Messages relevés sur clamonacc 1.5.4.
// Erreurs écrites sans « ERROR: » en tête.
const QLatin1String kUnprefixedErrors[] = {
    QLatin1String("Wait timeout exceeded;"), // --wait : clamd toujours injoignable après 30 s
};
// Erreurs sans conséquence : un dossier apparu puis supprimé avant que
// clamonacc ait pu le surveiller (par exemple safebrowsing-backup, que Firefox
// et ses dérivés créent et suppriment à chaque mise à jour de leurs listes).
// Les mêmes lignes précèdent l'erreur fatale « could not watch path », qui,
// elle, est signalée.
const QLatin1String kBenignErrors[] = {
    QLatin1String("ClamInotif: could not add element to hash table for "),
    QLatin1String("ClamInotif: watch descriptor issue when adding watch for "),
    QLatin1String("ClamInotif: issue when adding watch for "),
};

// text commence-t-il par l'un des préfixes ?
template<size_t N>
bool startsWithAny(const QString &text, const QLatin1String (&prefixes)[N])
{
    return std::any_of(std::begin(prefixes), std::end(prefixes),
                       [&text](QLatin1String prefix) { return text.startsWith(prefix); });
}

quint64 inodeOf(const QFile &file)
{
    struct stat info;
    return ::fstat(file.handle(), &info) == 0 ? quint64(info.st_ino) : 0;
}

// Dossier existant le plus proche du fichier : c'est lui qu'on surveille tant
// que le fichier n'existe pas (par exemple /var/log avant le premier lancement).
QString nearestExistingDirectory(const QString &file)
{
    QString dir = QFileInfo(file).absolutePath();
    while (!QFileInfo::exists(dir) && dir != QLatin1String("/"))
        dir = QFileInfo(dir).absolutePath();
    return dir;
}
}

OnAccessLog::OnAccessLog(const QString &path, QObject *parent)
    : QObject(parent)
    , m_path(path)
{
    const auto onChange = [this] {
        rearm();
        readNewData();
    };
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, onChange);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, onChange);
}

QString OnAccessLog::path() const
{
    return m_path;
}

std::optional<OnAccessLogLine> OnAccessLog::parseLine(const QString &line)
{
    QString text = line.trimmed();

    // Préfixe de date de ClamAV (« Wed Sep 24 10:12:13 2026 -> »), au cas où
    // une version future de clamonacc horodaterait son journal.
    static const QRegularExpression datePrefix(
        QStringLiteral("^\\w{3} \\w{3} [ \\d]\\d \\d\\d:\\d\\d:\\d\\d \\d{4} -> "));
    text.remove(datePrefix);

    OnAccessLogLine result;
    // Première ligne écrite par clamonacc à chaque lancement.
    static const QRegularExpression runSeparator(QStringLiteral("^-{10,}$"));
    if (runSeparator.match(text).hasMatch()) {
        result.type = OnAccessLogLine::Type::RunStart;
        return result;
    }

    if (text.startsWith(QLatin1String("ERROR: ")) || startsWithAny(text, kUnprefixedErrors)) {
        result.type = OnAccessLogLine::Type::Error;
        result.message = text.startsWith(QLatin1String("ERROR: ")) ? text.mid(7).trimmed() : text;
        result.benign = startsWithAny(result.message, kBenignErrors);
        return result;
    }
    if (text.endsWith(QLatin1String(" FOUND"))) {
        // « <chemin>: <menace> FOUND ». Le chemin peut lui-même contenir
        // « : », pas le nom de la menace : on coupe au dernier « : ».
        const QString body = text.chopped(6);
        const qsizetype separator = body.lastIndexOf(QLatin1String(": "));
        if (separator > 0) {
            result.type = OnAccessLogLine::Type::Detection;
            result.path = body.left(separator);
            result.threat = body.mid(separator + 2).trimmed();
            return result;
        }
    }
    return std::nullopt;
}

void OnAccessLog::start()
{
    QList<OnAccessDetection> history;
    QString lastError; // dernière erreur du dernier lancement
    QFile file(m_path);
    if (file.open(QIODevice::ReadOnly)) {
        m_inode = inodeOf(file);
        const qint64 size = file.size();
        const qint64 from = qMax<qint64>(0, size - kMaxHistoryBytes);
        file.seek(from);
        QByteArray data = file.read(size - from);
        m_offset = from + data.size();
        m_tail = data.right(kTailSize);

        const qsizetype lastNewline = data.lastIndexOf('\n');
        m_pending = data.mid(lastNewline + 1);
        data.truncate(lastNewline + 1);
        QList<QByteArray> lines = data.split('\n');
        if (from > 0 && !lines.isEmpty())
            lines.removeFirst(); // début de ligne coupé par la limite de lecture

        for (const QByteArray &raw : std::as_const(lines)) {
            const std::optional<OnAccessLogLine> line = parseLine(QString::fromUtf8(raw));
            if (!line)
                continue;
            if (line->type == OnAccessLogLine::Type::RunStart) {
                lastError.clear();
                continue;
            }
            if (line->type == OnAccessLogLine::Type::Error) {
                if (!line->benign)
                    lastError = line->message;
                continue;
            }
            // Doublon immédiat (écriture puis lecture du même fichier) : ignoré.
            if (!history.isEmpty() && history.last().path == line->path && history.last().threat == line->threat)
                continue;
            history.append({QDateTime(), line->path, line->threat});
        }
        if (history.size() > kMaxHistoryEntries)
            history = history.mid(history.size() - kMaxHistoryEntries);
    }

    rearm();
    emit historyLoaded(history);
    // Le service a pu échouer avant le lancement de l'application : la cause
    // est dans le journal.
    if (!lastError.isEmpty())
        emit errorLogged(lastError);
}

void OnAccessLog::rearm()
{
    QStringList wanted;
    if (QFileInfo::exists(m_path))
        wanted << m_path;
    wanted << nearestExistingDirectory(m_path);

    const QStringList watched = m_watcher.files() + m_watcher.directories();
    for (const QString &path : watched) {
        if (!wanted.contains(path))
            m_watcher.removePath(path);
    }
    for (const QString &path : std::as_const(wanted)) {
        if (!watched.contains(path))
            m_watcher.addPath(path);
    }
}

void OnAccessLog::readNewData()
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return; // pas encore créé, ou illisible

    // Fichier remplacé (rotation) ou vidé : on relit depuis le début. Un
    // fichier vidé puis réécrit avant notre lecture peut avoir retrouvé sa
    // taille : on vérifie donc aussi que les derniers octets lus sont intacts.
    const quint64 inode = inodeOf(file);
    bool rewritten = inode != m_inode || file.size() < m_offset;
    if (!rewritten && !m_tail.isEmpty()) {
        file.seek(m_offset - m_tail.size());
        rewritten = file.read(m_tail.size()) != m_tail;
    }
    if (rewritten) {
        m_inode = inode;
        m_offset = 0;
        m_pending.clear();
        m_tail.clear();
    }

    file.seek(m_offset);
    const QByteArray data = file.readAll();
    m_offset += data.size();
    m_tail = (m_tail + data).right(kTailSize);
    m_pending += data;

    const qsizetype lastNewline = m_pending.lastIndexOf('\n');
    if (lastNewline < 0)
        return; // ligne pas encore terminée
    const QList<QByteArray> lines = m_pending.left(lastNewline).split('\n');
    m_pending.remove(0, lastNewline + 1);

    for (const QByteArray &raw : lines) {
        const std::optional<OnAccessLogLine> line = parseLine(QString::fromUtf8(raw));
        if (!line)
            continue;
        switch (line->type) {
        case OnAccessLogLine::Type::RunStart:
            emit runStarted();
            break;
        case OnAccessLogLine::Type::Error:
            if (!line->benign)
                emit errorLogged(line->message);
            break;
        case OnAccessLogLine::Type::Detection:
            if (!isRecentDuplicate(line->path, line->threat))
                emit threatDetected({QDateTime::currentDateTime(), line->path, line->threat});
            break;
        }
    }
}

bool OnAccessLog::isRecentDuplicate(const QString &path, const QString &threat)
{
    const QString key = path + QLatin1Char('\n') + threat;
    const QDateTime now = QDateTime::currentDateTime();
    const bool duplicate = key == m_lastKey && m_lastTime.secsTo(now) < kDuplicateWindowSecs;
    m_lastKey = key;
    m_lastTime = now;
    return duplicate;
}
