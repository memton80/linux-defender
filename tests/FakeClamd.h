#pragma once

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QTest>

#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>

#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

/**
 * Faux clamd pour les tests : pas besoin de clamd installé.
 *
 * Écoute sur un socket Unix, dans son propre thread (les scans de ScanJob sont
 * bloquants et se font eux aussi dans un thread). Comme le vrai clamd, il
 * traite une commande par connexion puis ferme la connexion.
 *
 * - FILDES : reçoit le descripteur (message SCM_RIGHTS), lit le fichier et
 *   répond comme clamd : « FOUND » si le contenu contient kVirusMarker (menace),
 *   kSuspiciousMarker (programme potentiellement indésirable) ou
 *   kEncryptedMarker (archive chiffrée), « ERROR » s'il contient kErrorMarker,
 *   « OK » sinon. Comme clamd, un fichier plus gros que maxFileSize (s'il
 *   n'est pas nul) n'est pas lu : la réponse est « OK ».
 * - Toute autre commande : renvoie `reply` tel quel ; si `reply` est nul
 *   (QByteArray()), ne répond jamais (test du délai d'attente).
 *
 * Les marqueurs ne sont pas de vraies signatures : un antivirus qui scanne le
 * dépôt ne les détectera pas.
 */
class FakeClamd
{
public:
    static constexpr const char *kVirusMarker = "LINUX-DEFENDER-FAKE-VIRUS";
    static constexpr const char *kSuspiciousMarker = "LINUX-DEFENDER-FAKE-PUA";
    static constexpr const char *kEncryptedMarker = "LINUX-DEFENDER-FAKE-ENCRYPTED";
    static constexpr const char *kErrorMarker = "LINUX-DEFENDER-FAKE-ERROR";
    static constexpr const char *kVirusName = "Test.FakeVirus";
    static constexpr const char *kSuspiciousName = "PUA.Unix.Tool.FakePua";
    static constexpr const char *kEncryptedName = "Heuristics.Encrypted.Zip";

    // Taille au-delà de laquelle un fichier n'est pas lu (MaxFileSize de clamd) ; 0 = pas de limite.
    std::atomic<qint64> maxFileSize{0};

    FakeClamd(const QString &path, const QByteArray &reply)
        : m_path(QFile::encodeName(path))
        , m_reply(reply)
    {
        m_listener = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::strncpy(address.sun_path, m_path.constData(), sizeof(address.sun_path) - 1);
        ::unlink(m_path.constData());
        QVERIFY(::bind(m_listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
        QVERIFY(::listen(m_listener, 16) == 0);
        m_thread = std::thread([this] { serve(); });
    }

    ~FakeClamd()
    {
        m_stop = true;
        if (m_thread.joinable())
            m_thread.join();
        ::close(m_listener);
        ::unlink(m_path.constData());
    }

    // Commandes reçues, dans l'ordre (chacune avec son octet nul final).
    QByteArray received() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_received;
    }

    int filesScanned() const { return m_filesScanned; }

private:
    // Attend des données (50 ms max) ; false si le test se termine.
    bool waitReadable(int fd) const
    {
        while (!m_stop) {
            pollfd pfd{fd, POLLIN, 0};
            if (::poll(&pfd, 1, 50) > 0)
                return true;
        }
        return false;
    }

    // Lit des données et, s'il y en a un, le descripteur de fichier joint.
    ssize_t receive(int client, QByteArray *data, int *receivedFd) const
    {
        char buffer[4096];
        iovec iov{buffer, sizeof(buffer)};
        union {
            cmsghdr header;
            char control[CMSG_SPACE(sizeof(int))];
        } control{};
        msghdr message{};
        message.msg_iov = &iov;
        message.msg_iovlen = 1;
        message.msg_control = control.control;
        message.msg_controllen = sizeof(control.control);

        const ssize_t n = ::recvmsg(client, &message, MSG_CMSG_CLOEXEC);
        if (n > 0)
            data->append(buffer, int(n));
        for (cmsghdr *cmsg = CMSG_FIRSTHDR(&message); cmsg; cmsg = CMSG_NXTHDR(&message, cmsg)) {
            if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS)
                std::memcpy(receivedFd, CMSG_DATA(cmsg), sizeof(int));
        }
        return n;
    }

    void serve()
    {
        while (waitReadable(m_listener)) {
            const int client = ::accept4(m_listener, nullptr, nullptr, SOCK_CLOEXEC);
            if (client >= 0) {
                handle(client);
                ::close(client);
            }
        }
    }

    void handle(int client)
    {
        QByteArray data;
        int fd = -1;
        while (!data.contains('\0')) {
            if (!waitReadable(client) || receive(client, &data, &fd) <= 0)
                return;
        }
        const QByteArray command = data.left(data.indexOf('\0'));
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_received += command + '\0';
        }

        QByteArray reply;
        if (command == "zFILDES") {
            // Le descripteur arrive avec un octet nul, parfois dans un second message.
            while (fd < 0) {
                if (!waitReadable(client) || receive(client, &data, &fd) <= 0)
                    return;
            }
            QByteArray content;
            struct stat info;
            const bool tooBig = maxFileSize > 0 && ::fstat(fd, &info) == 0 && info.st_size > maxFileSize;
            char buffer[4096];
            ssize_t n;
            while (!tooBig && (n = ::read(fd, buffer, sizeof(buffer))) > 0)
                content.append(buffer, int(n));
            const QByteArray name = "fd[" + QByteArray::number(fd) + "]: ";
            ::close(fd);
            ++m_filesScanned;
            if (content.contains(kVirusMarker))
                reply = name + kVirusName + " FOUND";
            else if (content.contains(kSuspiciousMarker))
                reply = name + kSuspiciousName + " FOUND";
            else if (content.contains(kEncryptedMarker))
                reply = name + kEncryptedName + " FOUND";
            else if (content.contains(kErrorMarker))
                reply = name + "Fake read error ERROR";
            else
                reply = name + "OK";
            reply += '\0';
        } else if (m_reply.isNull()) {
            // Ne répond jamais : attend que le client abandonne ou que le test se termine.
            char byte;
            while (waitReadable(client) && ::recv(client, &byte, 1, 0) > 0) { }
            return;
        } else {
            reply = m_reply;
        }
        ::send(client, reply.constData(), size_t(reply.size()), MSG_NOSIGNAL);
    }

    const QByteArray m_path;
    const QByteArray m_reply;
    int m_listener = -1;
    std::thread m_thread;
    std::atomic_bool m_stop{false};
    std::atomic_int m_filesScanned{0};
    mutable std::mutex m_mutex;
    QByteArray m_received;
};
