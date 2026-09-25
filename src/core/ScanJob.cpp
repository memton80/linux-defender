#include "ScanJob.h"

#include "ClamdClient.h"
#include "ClamdConfig.h"
#include "ThreatText.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QLocalSocket>
#include <QQueue>
#include <QThread>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr int kConnectTimeout = 5000;     // connexion à clamd
constexpr int kFileTimeout = 5 * 60000;   // analyse d'un fichier par clamd
constexpr int kFlushInterval = 100;       // envoi des résultats à l'interface (ms)
constexpr int kMaxBatchSize = 500;

// Dossiers système virtuels : leurs « fichiers » n'ont pas de sens pour un antivirus.
const QStringList &excludedDirectories()
{
    static const QStringList dirs = {
        QStringLiteral("/proc"),
        QStringLiteral("/sys"),
        QStringLiteral("/dev"),
    };
    return dirs;
}

QString errnoString(int error)
{
    return QString::fromLocal8Bit(std::strerror(error));
}

// Exclusions sous forme canonique : c'est sous cette forme que le parcours
// rencontre les chemins. Un chemin inexistant est gardé tel quel (nettoyé).
ScanOptions canonicalOptions(ScanOptions options)
{
    QStringList excluded;
    for (const QString &path : std::as_const(options.excludedPaths)) {
        if (path.isEmpty())
            continue;
        const QString canonical = QFileInfo(path).canonicalFilePath();
        excluded << (canonical.isEmpty() ? QDir::cleanPath(path) : canonical);
    }
    excluded.removeDuplicates();
    options.excludedPaths = excluded;
    return options;
}

// `path` est-il `directory` ou un élément de son contenu ?
bool isInside(const QString &path, const QString &directory)
{
    if (directory == QLatin1String("/"))
        return true;
    return path.startsWith(directory)
        && (path.size() == directory.size() || path.at(directory.size()) == QLatin1Char('/'));
}

// Ferme un descripteur de fichier en fin de portée.
struct FileDescriptorGuard
{
    int fd;
    ~FileDescriptorGuard() { ::close(fd); }
};

// Transmet `fd` à clamd par le socket Unix `socketFd` : un octet nul accompagné
// d'un message SCM_RIGHTS contenant le descripteur, comme clamdscan --fdpass.
bool sendFileDescriptor(int socketFd, int fd)
{
    char dummy = '\0';
    iovec iov{&dummy, 1};

    union {
        cmsghdr header; // garantit l'alignement du tampon
        char buffer[CMSG_SPACE(sizeof(int))];
    } control{};

    msghdr message{};
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    message.msg_control = control.buffer;
    message.msg_controllen = sizeof(control.buffer);

    cmsghdr *cmsg = CMSG_FIRSTHDR(&message);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));

    // Le socket de Qt est non bloquant : si son tampon est plein, on attend un peu.
    for (int attempt = 0; attempt < 50; ++attempt) {
        const ssize_t sent = ::sendmsg(socketFd, &message, MSG_NOSIGNAL);
        if (sent == 1)
            return true;
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            pollfd pfd{socketFd, POLLOUT, 0};
            ::poll(&pfd, 1, 100);
        } else if (errno != EINTR) {
            return false;
        }
    }
    errno = ETIMEDOUT;
    return false;
}

} // namespace

ScanJob::ScanJob(const QString &socketPath, const QStringList &paths, const ScanOptions &options, QObject *parent)
    : QObject(parent)
    , m_socketPath(socketPath)
    , m_paths(paths)
    , m_options(canonicalOptions(options))
{
}

ScanJob::~ScanJob()
{
    cancel();
    if (m_thread)
        m_thread->wait();
}

QStringList ScanJob::paths() const
{
    return m_paths;
}

void ScanJob::start()
{
    Q_ASSERT(!m_thread);
    m_thread = QThread::create([this] { run(); });
    m_thread->setParent(this);
    // Priorité basse : un scan en arrière-plan ne doit pas ralentir la session.
    m_thread->start(QThread::LowPriority);
}

void ScanJob::cancel()
{
    m_cancelled = true;
}

std::optional<ScanResult> ScanJob::parseReply(const QString &path, const QByteArray &reply)
{
    // Réponses possibles (sans l'octet nul final) :
    //   "fd[10]: OK"
    //   "fd[10]: Win.Test.EICAR_HDB-1 FOUND"
    //   "fd[10]: Can't allocate memory ERROR"
    //   "No file descriptor received. ERROR"
    // « fd[10] » est le nom que clamd donne au descripteur reçu : on le retire.
    QString text = QString::fromUtf8(reply).trimmed();
    if (text.startsWith(QLatin1String("fd["))) {
        const qsizetype separator = text.indexOf(QLatin1String(": "));
        if (separator >= 0)
            text = text.mid(separator + 2);
    }

    ScanResult result;
    result.path = path;
    if (text == QLatin1String("OK")) {
        result.status = ScanResult::Status::Clean;
    } else if (text.endsWith(QLatin1String(" FOUND"))) {
        result.detail = text.chopped(6).trimmed();
        switch (ThreatText::kind(result.detail)) {
        case ThreatText::Kind::Threat:
            result.status = ScanResult::Status::Infected;
            break;
        case ThreatText::Kind::Suspicious:
            result.status = ScanResult::Status::Suspicious;
            break;
        case ThreatText::Kind::Unscanned:
            result.status = ScanResult::Status::Unscanned;
            break;
        }
    } else if (text.endsWith(QLatin1String(" ERROR"))) {
        result.status = ScanResult::Status::Error;
        result.detail = tr("Erreur de clamd : %1").arg(text.chopped(6).trimmed());
    } else {
        return std::nullopt;
    }
    return result;
}

QString ScanJob::unscannedSizeText(qint64 limit)
{
    if (limit >= ClamdConfig::kEngineMaxFileSize)
        return tr("Non analysé : plus gros que ce que clamd sait analyser (2 Go)");
    return tr("Non analysé : plus gros que la limite de clamd (%1 Mo, directive MaxFileSize)")
        .arg(QLocale().toString(double(limit) / (1024 * 1024), 'g', 4));
}

void ScanJob::run()
{
    ScanSummary summary;
    summary.paths = m_paths;
    summary.started = QDateTime::currentDateTime();
    QElapsedTimer duration;
    duration.start();
    const auto finish = [&] {
        summary.elapsedMsecs = duration.elapsed();
        emit finished(summary);
    };

    // 1. clamd répond-il ?
    const Reply ping = request(QByteArrayLiteral("zPING"), -1, kConnectTimeout);
    if (m_cancelled) {
        summary.cancelled = true;
        finish();
        return;
    }
    if (!ping.error.isEmpty() || ping.data != "PONG") {
        summary.fatalError = !ping.error.isEmpty()
            ? ping.error
            : ClamdClient::errorMessage(ClamdClient::Error::ProtocolError, m_socketPath, QString::fromUtf8(ping.data));
        finish();
        return;
    }

    // 2. Comptage des fichiers.
    qint64 total = 0;
    QElapsedTimer sinceSignal;
    sinceSignal.start();
    for (const QString &root : m_paths) {
        walk(root, [&](const QString &) {
            ++total;
            if (sinceSignal.hasExpired(kFlushInterval)) {
                emit counting(total);
                sinceSignal.restart();
            }
            return true;
        }, nullptr);
    }
    emit counting(total);

    // 3. Scan. Les résultats sont regroupés et envoyés au plus toutes les 100 ms.
    QList<ScanResult> batch;
    auto flush = [&] {
        if (!batch.isEmpty()) {
            emit resultsReady(batch);
            batch.clear();
        }
        emit progressChanged(summary.scanned, qMax(total, summary.scanned));
        sinceSignal.restart();
    };
    auto add = [&](ScanResult result) {
        switch (result.status) {
        case ScanResult::Status::Infected:
            ++summary.infected;
            if (summary.threats.size() < ScanSummary::kMaxThreats)
                summary.threats.append(result);
            break;
        case ScanResult::Status::Suspicious:
        case ScanResult::Status::Unscanned:
            ++(result.status == ScanResult::Status::Suspicious ? summary.suspicious : summary.unscanned);
            if (summary.warnings.size() < ScanSummary::kMaxThreats)
                summary.warnings.append(result);
            break;
        case ScanResult::Status::Error:
            ++summary.errors;
            break;
        case ScanResult::Status::Clean:
            break;
        }
        batch.append(std::move(result));
        if (sinceSignal.hasExpired(kFlushInterval) || batch.size() >= kMaxBatchSize)
            flush();
    };

    const auto onFile = [&](const QString &path) {
        if (m_cancelled)
            return false;
        ScanResult result = scanFile(path, &summary.fatalError);
        if (m_cancelled || !summary.fatalError.isEmpty())
            return false; // résultat incomplet : on ne le compte pas
        ++summary.scanned;
        add(std::move(result));
        return true;
    };
    const auto onError = [&](const QString &path, const QString &error) {
        add(ScanResult{path, ScanResult::Status::Error, error});
        return true;
    };

    for (const QString &root : m_paths) {
        if (!walk(root, onFile, onError, &summary.skipped))
            break;
    }

    flush();
    summary.cancelled = m_cancelled;
    finish();
}

bool ScanJob::walk(const QString &root, const FileVisitor &onFile, const ErrorVisitor &onError, qint64 *skipped)
{
    // canonicalFilePath() résout les liens symboliques choisis explicitement
    // par l'utilisateur ; il est vide si le chemin n'existe pas.
    const QFileInfo rootInfo(root);
    const QString start = rootInfo.canonicalFilePath();
    if (start.isEmpty())
        return !onError || onError(root, tr("Introuvable"));
    if (!rootInfo.isDir())
        return onFile(start);

    // Exclusions qui contiennent le dossier choisi : l'utilisateur a demandé
    // explicitement ce dossier, elles ne s'appliquent pas à son contenu.
    QStringList excluded;
    for (const QString &path : m_options.excludedPaths) {
        if (!isInside(start, path))
            excluded << path;
    }
    const auto isExcluded = [&excluded](const QString &path) {
        return std::any_of(excluded.cbegin(), excluded.cend(),
                           [&path](const QString &directory) { return isInside(path, directory); });
    };

    QQueue<QString> directories;
    directories.enqueue(start);
    while (!directories.isEmpty()) {
        if (m_cancelled)
            return false;

        const QString directory = directories.dequeue();
        if (::access(QFile::encodeName(directory).constData(), R_OK | X_OK) != 0) {
            const int error = errno;
            if (onError && !onError(directory, tr("Dossier illisible : %1").arg(errnoString(error))))
                return false;
            continue;
        }

        QDir::Filters filters = QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot;
        if (m_options.scanHidden)
            filters |= QDir::Hidden;
        QDirIterator it(directory, filters);
        while (it.hasNext()) {
            const QString path = it.next();
            const QFileInfo info = it.fileInfo();
            if (info.isSymLink())
                continue; // jamais suivis : évite les boucles et les sorties du dossier
            if (isExcluded(path))
                continue;
            if (info.isDir()) {
                if (!excludedDirectories().contains(path))
                    directories.enqueue(path);
            } else if (info.isFile()) {
                if (m_options.maxFileSize > 0 && info.size() > m_options.maxFileSize) {
                    if (skipped)
                        ++*skipped;
                    continue;
                }
                if (!onFile(path))
                    return false;
            }
            // Les autres entrées (FIFO, socket, périphérique) sont ignorées.
        }
    }
    return true;
}

ScanResult ScanJob::scanFile(const QString &path, QString *fatalError)
{
    ScanResult result{path, ScanResult::Status::Error, {}};

    // Ouverture avec les droits de l'utilisateur.
    // O_NOFOLLOW : refuse un lien symbolique apparu depuis le parcours.
    // O_NONBLOCK : ne jamais rester bloqué sur un fichier spécial (FIFO...).
    const int fd = ::open(QFile::encodeName(path).constData(),
                          O_RDONLY | O_CLOEXEC | O_NOCTTY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        result.detail = tr("Ouverture impossible : %1").arg(errnoString(errno));
        return result;
    }
    const FileDescriptorGuard guard{fd};

    struct stat info;
    if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)) {
        result.detail = tr("Pas un fichier ordinaire");
        return result;
    }

    const Reply reply = request(QByteArrayLiteral("zFILDES"), fd, kFileTimeout);
    if (reply.fatal) {
        *fatalError = reply.error;
        return result;
    }
    if (!reply.error.isEmpty()) {
        result.detail = reply.error;
        return result;
    }

    std::optional<ScanResult> parsed = parseReply(path, reply.data);
    if (!parsed) {
        // clamd ne comprend pas FILDES : les fichiers suivants échoueraient tous.
        *fatalError = ClamdClient::errorMessage(ClamdClient::Error::ProtocolError, m_socketPath,
                                                QString::fromUtf8(reply.data));
        return result;
    }
    // Au-delà de sa limite, clamd répond « OK » sans avoir lu le fichier
    // (sauf avec AlertExceedsMax) : il n'est pas sain, il est non analysé.
    const qint64 limit = m_options.clamdUnscannedAbove;
    if (parsed->status == ScanResult::Status::Clean && limit > 0 && info.st_size > limit) {
        parsed->status = ScanResult::Status::Unscanned;
        parsed->detail = unscannedSizeText(limit);
    }
    return *parsed;
}

ScanJob::Reply ScanJob::request(const QByteArray &command, int fileDescriptor, int timeoutMsecs)
{
    Reply reply;
    // Une connexion par commande, comme clamdscan : clamd la ferme après avoir répondu.
    QLocalSocket socket;
    socket.connectToServer(m_socketPath);
    if (!socket.waitForConnected(kConnectTimeout)) {
        reply.fatal = true;
        reply.error = ClamdClient::errorMessage(ClamdClient::errorFromSocket(socket.error()), m_socketPath,
                                                socket.errorString());
        return reply;
    }

    socket.write(command + '\0');
    if (!socket.waitForBytesWritten(kConnectTimeout)) {
        reply.fatal = true;
        reply.error = ClamdClient::errorMessage(ClamdClient::Error::SocketError, m_socketPath, socket.errorString());
        return reply;
    }
    if (fileDescriptor >= 0 && !sendFileDescriptor(int(socket.socketDescriptor()), fileDescriptor)) {
        reply.error = tr("Envoi du fichier à clamd impossible : %1").arg(errnoString(errno));
        return reply;
    }

    // Attente de la réponse par tranches de 200 ms, pour pouvoir annuler.
    QElapsedTimer elapsed;
    elapsed.start();
    while (!reply.data.contains('\0')) {
        if (m_cancelled) {
            reply.error = tr("Annulé");
            return reply;
        }
        if (socket.bytesAvailable() > 0 || socket.waitForReadyRead(200)) {
            reply.data += socket.readAll();
        } else if (socket.state() != QLocalSocket::ConnectedState) {
            break; // clamd a fermé la connexion
        } else if (elapsed.hasExpired(timeoutMsecs)) {
            reply.error = ClamdClient::errorMessage(ClamdClient::Error::Timeout, m_socketPath);
            return reply;
        }
    }

    const qsizetype end = reply.data.indexOf('\0');
    if (end >= 0)
        reply.data.truncate(end);
    else if (reply.data.isEmpty())
        reply.error = tr("clamd a fermé la connexion sans répondre");
    return reply;
}
