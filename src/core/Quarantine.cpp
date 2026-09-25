#include "Quarantine.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
constexpr int kFormatVersion = 1;
constexpr qsizetype kChunkSize = 1024 * 1024;
constexpr qint64 kRecentSecs = 60;
// En-tête des fichiers brouillés : reconnaît un fichier de quarantaine.
const QByteArray kMagic = QByteArrayLiteral("LINUX-DEFENDER-QUARANTINE-1\n");
// Clé du brouillage : il rend le fichier inerte, il ne le protège pas d'un curieux.
const QByteArray kKey = QByteArrayLiteral("Linux Defender : fichier en quarantaine.");

QString errnoString(int error)
{
    return QString::fromLocal8Bit(std::strerror(error));
}

struct FileDescriptorGuard
{
    int fd;
    ~FileDescriptorGuard()
    {
        if (fd >= 0)
            ::close(fd);
    }
};

// Brouille (ou débrouille : l'opération est son propre inverse) un morceau
// qui commence à `offset` dans le fichier.
void scramble(QByteArray &chunk, qint64 offset)
{
    char *data = chunk.data();
    for (qsizetype i = 0; i < chunk.size(); ++i)
        data[i] = char(data[i] ^ kKey.at((offset + i) % kKey.size()));
}

QString dataPath(const QString &directory, const QString &id)
{
    return QDir(directory).filePath(id + QStringLiteral(".bin"));
}

QString metadataPath(const QString &directory, const QString &id)
{
    return QDir(directory).filePath(id + QStringLiteral(".json"));
}

void removeFiles(const QString &directory, const QString &id)
{
    QFile::remove(dataPath(directory, id));
    QFile::remove(metadataPath(directory, id));
}

bool writeFile(const QString &path, const QByteArray &content)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        && file.write(content) == content.size() && file.commit();
}

// « facture.pdf » -> « facture (restauré).pdf », « facture (restauré 2).pdf »...
// Point cherché après le premier caractère : un fichier caché (« .bashrc ») garde son nom entier.
QString alternativePath(const QString &path, int attempt)
{
    const QFileInfo info(path);
    QString name = info.fileName();
    const QString tag = attempt == 1 ? Quarantine::tr(" (restauré)") : Quarantine::tr(" (restauré %1)").arg(attempt);
    const qsizetype dot = name.indexOf(QLatin1Char('.'), 1);
    name = dot < 0 ? name + tag : name.left(dot) + tag + name.mid(dot);
    return info.dir().filePath(name);
}

bool exists(const QString &path)
{
    struct stat info;
    return ::lstat(QFile::encodeName(path).constData(), &info) == 0;
}
}

Quarantine::Quarantine(const QString &directory, QObject *parent)
    : QObject(parent)
    , m_directory(directory)
{
    reload();
}

Quarantine::~Quarantine()
{
    m_jobs.clear();
    if (m_thread)
        m_thread->wait();
}

QString Quarantine::defaultDirectory()
{
    // Même dossier que l'historique : ~/.local/share/linux-defender.
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/linux-defender/quarantine");
}

QString Quarantine::directory() const
{
    return m_directory;
}

QList<QuarantineEntry> Quarantine::entries() const
{
    return m_entries;
}

bool Quarantine::contains(const QString &originalPath) const
{
    return m_originalPaths.contains(originalPath);
}

bool Quarantine::isRecentlyQuarantined(const QString &path) const
{
    const QDateTime when = m_recent.value(path);
    return when.isValid() && when.secsTo(QDateTime::currentDateTime()) < kRecentSecs;
}

bool Quarantine::isBusy() const
{
    return m_thread != nullptr || !m_jobs.isEmpty();
}

void Quarantine::add(const QList<Item> &items)
{
    const QString directory = m_directory;
    enqueue([this, directory, items] {
        for (const Item &item : items) {
            QuarantineEntry entry;
            QString error;
            addFile(directory, item, &entry, &error);
            // Résultat traité dans le thread de l'objet (celui de l'interface).
            QMetaObject::invokeMethod(this, [this, item, error] {
                if (error.isEmpty())
                    m_recent.insert(item.path, QDateTime::currentDateTime());
                reload();
                emit finished(Operation::Add, item.path, error);
            }, Qt::QueuedConnection);
        }
    });
}

void Quarantine::restore(const QString &id)
{
    const QString directory = m_directory;
    enqueue([this, directory, id] {
        QString restoredPath;
        QString error;
        restoreFile(directory, id, &restoredPath, &error);
        QMetaObject::invokeMethod(this, [this, restoredPath, error] {
            reload();
            emit finished(Operation::Restore, restoredPath, error);
        }, Qt::QueuedConnection);
    });
}

void Quarantine::remove(const QString &id)
{
    const QString directory = m_directory;
    QString originalPath;
    for (const QuarantineEntry &entry : std::as_const(m_entries)) {
        if (entry.id == id)
            originalPath = entry.originalPath;
    }
    enqueue([this, directory, id, originalPath] {
        QString error;
        removeFile(directory, id, &error);
        QMetaObject::invokeMethod(this, [this, originalPath, error] {
            reload();
            emit finished(Operation::Remove, originalPath, error);
        }, Qt::QueuedConnection);
    });
}

void Quarantine::enqueue(const std::function<void()> &job)
{
    m_jobs.enqueue(job);
    startNext();
}

void Quarantine::startNext()
{
    if (m_thread || m_jobs.isEmpty())
        return;
    const std::function<void()> job = m_jobs.dequeue();
    m_thread = QThread::create(job);
    // Émis après les résultats de l'opération, déjà dans la file d'événements.
    connect(m_thread, &QThread::finished, this, [this] {
        m_thread->deleteLater();
        m_thread = nullptr;
        if (m_jobs.isEmpty())
            emit idle();
        startNext();
    });
    m_thread->start(QThread::LowPriority);
}

void Quarantine::reload()
{
    m_entries = readEntries(m_directory);
    m_originalPaths.clear();
    for (const QuarantineEntry &entry : std::as_const(m_entries))
        m_originalPaths.insert(entry.originalPath);
    emit changed();
}

QList<QuarantineEntry> Quarantine::readEntries(const QString &directory)
{
    QList<QuarantineEntry> entries;
    const QDir dir(directory);
    for (const QString &name : dir.entryList({QStringLiteral("*.json")}, QDir::Files)) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
        QuarantineEntry entry;
        entry.id = QFileInfo(name).completeBaseName();
        entry.originalPath = object.value(QStringLiteral("originalPath")).toString();
        entry.threat = object.value(QStringLiteral("threat")).toString();
        entry.date = QDateTime::fromString(object.value(QStringLiteral("date")).toString(), Qt::ISODateWithMs);
        entry.size = object.value(QStringLiteral("size")).toInteger();
        entry.sha256 = object.value(QStringLiteral("sha256")).toString();
        entry.permissions = uint(object.value(QStringLiteral("permissions")).toInt());
        // Fichier brouillé absent : l'entrée ne sert à rien.
        if (!entry.originalPath.isEmpty() && QFile::exists(dataPath(directory, entry.id)))
            entries << entry;
    }
    std::sort(entries.begin(), entries.end(),
              [](const QuarantineEntry &a, const QuarantineEntry &b) { return a.date > b.date; });
    return entries;
}

bool Quarantine::addFile(const QString &directory, const Item &item, QuarantineEntry *entry, QString *error)
{
    const QByteArray path = QFile::encodeName(item.path);
    // O_NOFOLLOW : jamais un lien symbolique (on déplacerait sa cible).
    const int fd = ::open(path.constData(), O_RDONLY | O_CLOEXEC | O_NOCTTY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        *error = tr("Ouverture impossible de %1 : %2").arg(item.path, errnoString(errno));
        return false;
    }
    const FileDescriptorGuard guard{fd};
    struct stat original;
    if (::fstat(fd, &original) != 0 || !S_ISREG(original.st_mode)) {
        *error = tr("%1 n'est pas un fichier ordinaire.").arg(item.path);
        return false;
    }
    // Sans droit d'écriture sur son dossier, le fichier ne pourra pas être
    // retiré : inutile de le copier.
    const QString parent = QFileInfo(item.path).absolutePath();
    if (::access(QFile::encodeName(parent).constData(), W_OK | X_OK) != 0) {
        *error = tr("Le dossier %1 n'est pas modifiable par votre utilisateur : le fichier ne peut pas en être "
                    "retiré (fichier d'un autre utilisateur, ou support en lecture seule).")
                     .arg(parent);
        return false;
    }
    if (!QDir().mkpath(directory)
        || !QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) {
        *error = tr("Création impossible du dossier de quarantaine %1.").arg(directory);
        return false;
    }

    // Copie brouillée, et empreinte du contenu d'origine.
    entry->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSaveFile data(dataPath(directory, entry->id));
    if (!data.open(QIODevice::WriteOnly) || !data.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || data.write(kMagic) != kMagic.size()) {
        *error = tr("Écriture impossible dans la quarantaine : %1").arg(data.errorString());
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray buffer(kChunkSize, Qt::Uninitialized);
    qint64 size = 0;
    ssize_t count = 0;
    while ((count = ::read(fd, buffer.data(), size_t(buffer.size()))) > 0) {
        QByteArray chunk = buffer.left(count);
        hash.addData(chunk);
        scramble(chunk, size);
        if (data.write(chunk) != chunk.size())
            break;
        size += count;
    }
    if (count < 0) {
        const int readError = errno;
        data.cancelWriting();
        *error = tr("Lecture impossible de %1 : %2").arg(item.path, errnoString(readError));
        return false;
    }
    if (!data.commit()) {
        *error = tr("Écriture impossible dans la quarantaine : %1").arg(data.errorString());
        return false;
    }

    entry->originalPath = item.path;
    entry->threat = item.threat;
    entry->date = QDateTime::currentDateTime();
    entry->size = size;
    entry->sha256 = QString::fromLatin1(hash.result().toHex());
    entry->permissions = uint(original.st_mode & 0777);
    const QJsonObject metadata{
        {QStringLiteral("version"), kFormatVersion},
        {QStringLiteral("originalPath"), entry->originalPath},
        {QStringLiteral("threat"), entry->threat},
        {QStringLiteral("date"), entry->date.toString(Qt::ISODateWithMs)},
        {QStringLiteral("size"), entry->size},
        {QStringLiteral("sha256"), entry->sha256},
        {QStringLiteral("permissions"), int(entry->permissions)},
    };
    if (!writeFile(metadataPath(directory, entry->id), QJsonDocument(metadata).toJson())) {
        removeFiles(directory, entry->id);
        *error = tr("Écriture impossible dans la quarantaine %1.").arg(directory);
        return false;
    }

    // Toujours le même fichier à cet emplacement ? (Remplacé entre-temps : on
    // supprimerait un autre fichier que celui qui a été copié.)
    struct stat current;
    if (::lstat(path.constData(), &current) != 0 || current.st_dev != original.st_dev
        || current.st_ino != original.st_ino) {
        removeFiles(directory, entry->id);
        *error = tr("%1 a changé pendant sa mise en quarantaine : rien n'a été fait.").arg(item.path);
        return false;
    }
    if (::unlink(path.constData()) != 0) {
        const int unlinkError = errno;
        removeFiles(directory, entry->id);
        *error = tr("Suppression impossible de %1 : %2").arg(item.path, errnoString(unlinkError));
        return false;
    }
    return true;
}

bool Quarantine::restoreFile(const QString &directory, const QString &id, QString *restoredPath, QString *error)
{
    const QList<QuarantineEntry> entries = readEntries(directory);
    const auto found = std::find_if(entries.cbegin(), entries.cend(), [&id](const QuarantineEntry &entry) {
        return entry.id == id;
    });
    if (found == entries.cend()) {
        *error = tr("Ce fichier n'est plus dans la quarantaine.");
        return false;
    }
    const QuarantineEntry entry = *found;

    QFile data(dataPath(directory, id));
    if (!data.open(QIODevice::ReadOnly) || data.read(kMagic.size()) != kMagic) {
        *error = tr("Fichier de quarantaine illisible : %1").arg(data.fileName());
        return false;
    }

    // Emplacement libre : l'original, sinon un nom voisin. Jamais d'écrasement.
    QDir().mkpath(QFileInfo(entry.originalPath).absolutePath());
    QString target = entry.originalPath;
    for (int attempt = 1; exists(target); ++attempt)
        target = alternativePath(entry.originalPath, attempt);
    const QByteArray targetName = QFile::encodeName(target);
    const int fd = ::open(targetName.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        *error = tr("Restauration impossible vers %1 : %2").arg(target, errnoString(errno));
        return false;
    }
    FileDescriptorGuard guard{fd};

    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 offset = 0;
    bool written = true;
    while (written && !data.atEnd()) {
        QByteArray chunk = data.read(kChunkSize);
        if (chunk.isEmpty())
            break;
        scramble(chunk, offset);
        hash.addData(chunk);
        offset += chunk.size();
        written = ::write(fd, chunk.constData(), size_t(chunk.size())) == chunk.size();
    }
    // Droits d'origine, sans setuid, setgid ni sticky.
    written = written && ::fchmod(fd, mode_t(entry.permissions & 0777)) == 0;
    const int closeResult = ::close(guard.fd);
    guard.fd = -1;
    if (!written || closeResult != 0 || QString::fromLatin1(hash.result().toHex()) != entry.sha256) {
        ::unlink(targetName.constData());
        *error = written && closeResult == 0
            ? tr("Le fichier en quarantaine est abîmé (empreinte différente) : il n'a pas été restauré.")
            : tr("Écriture impossible de %1.").arg(target);
        return false;
    }

    removeFiles(directory, id);
    *restoredPath = target;
    return true;
}

bool Quarantine::removeFile(const QString &directory, const QString &id, QString *error)
{
    if (!QFile::remove(dataPath(directory, id)) && QFile::exists(dataPath(directory, id))) {
        *error = tr("Suppression impossible de %1.").arg(dataPath(directory, id));
        return false;
    }
    QFile::remove(metadataPath(directory, id));
    return true;
}
