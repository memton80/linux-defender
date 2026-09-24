#include "DesktopNotifier.h"

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QVariantMap>

namespace
{
const QString kPath = QStringLiteral("/org/freedesktop/Notifications");
const QString kInterface = QStringLiteral("org.freedesktop.Notifications");
}

DesktopNotifier::DesktopNotifier(const QString &appName, const QString &desktopEntry, const QDBusConnection &bus,
                                 QObject *parent)
    : QObject(parent)
    , m_appName(appName)
    , m_desktopEntry(desktopEntry)
    , m_bus(bus)
{
    if (!m_bus.isConnected())
        return;
    const QString service = QString::fromLatin1(kService);
    m_bus.connect(service, kPath, kInterface, QStringLiteral("ActionInvoked"), this,
                  SLOT(onActionInvoked(uint,QString)));
    m_bus.connect(service, kPath, kInterface, QStringLiteral("NotificationClosed"), this,
                  SLOT(onNotificationClosed(uint,uint)));
    m_bus.connect(service, kPath, kInterface, QStringLiteral("ActivationToken"), this,
                  SLOT(onActivationToken(uint,QString)));

    // Le serveur interprète-t-il le HTML dans le texte ? Si oui, le texte (qui
    // contient des noms de fichiers) doit être échappé.
    if (!isAvailable())
        return;
    auto *watcher = new QDBusPendingCallWatcher(
        m_bus.asyncCall(QDBusMessage::createMethodCall(service, kPath, kInterface, QStringLiteral("GetCapabilities"))),
        this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        const QDBusPendingReply<QStringList> reply = *watcher;
        if (!reply.isError())
            m_bodyMarkup = reply.value().contains(QLatin1String("body-markup"));
    });
}

bool DesktopNotifier::isAvailable() const
{
    QDBusConnectionInterface *daemon = m_bus.isConnected() ? m_bus.interface() : nullptr;
    if (!daemon)
        return false;
    // Service lancé (plasmashell, gnome-shell) ou démarré à la demande (dunst, mako...).
    const QString service = QString::fromLatin1(kService);
    return daemon->isServiceRegistered(service).value()
        || daemon->activatableServiceNames().value().contains(service);
}

bool DesktopNotifier::show(const QString &key, const Notification &notification)
{
    if (!isAvailable())
        return false;
    Entry &entry = m_entries[key];
    entry.closeRequested = false;
    if (entry.pending) {
        // L'identifiant de la notification affichée n'est pas encore connu :
        // la mise à jour attend la réponse du serveur, pour la remplacer.
        entry.queued = notification;
        return true;
    }
    send(key, notification);
    return true;
}

void DesktopNotifier::close(const QString &key)
{
    const auto it = m_entries.find(key);
    if (it == m_entries.end())
        return;
    if (it->pending) {
        it->closeRequested = true;
        it->queued.reset();
        return;
    }
    closeId(it->id);
    m_entries.erase(it);
}

void DesktopNotifier::send(const QString &key, const Notification &notification)
{
    Entry &entry = m_entries[key];
    entry.pending = true;

    QStringList actions; // clé, libellé, clé, libellé...
    for (const auto &[action, label] : notification.actions)
        actions << action << label;
    QVariantMap hints;
    hints.insert(QStringLiteral("urgency"), QVariant::fromValue(uchar(notification.urgency))); // octet (« y »)
    hints.insert(QStringLiteral("desktop-entry"), m_desktopEntry);

    QDBusMessage call = QDBusMessage::createMethodCall(QString::fromLatin1(kService), kPath, kInterface,
                                                       QStringLiteral("Notify"));
    call << m_appName << entry.id << notification.icon << notification.title
         << (m_bodyMarkup ? notification.body.toHtmlEscaped() : notification.body) << actions << hints
         << notification.timeoutMsecs;

    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, key, notification](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        const QDBusPendingReply<uint> reply = *watcher;
        if (reply.isError()) {
            m_entries.remove(key);
            emit failed(key, notification);
            return;
        }
        Entry &entry = m_entries[key];
        entry.id = reply.value();
        entry.pending = false;
        if (entry.closeRequested) {
            closeId(entry.id);
            m_entries.remove(key);
        } else if (entry.queued) {
            const Notification next = *entry.queued;
            entry.queued.reset();
            send(key, next);
        }
    });
}

void DesktopNotifier::closeId(uint id)
{
    if (id == 0)
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(QString::fromLatin1(kService), kPath, kInterface,
                                                       QStringLiteral("CloseNotification"));
    call << id;
    m_bus.asyncCall(call);
    m_tokens.remove(id);
}

QString DesktopNotifier::keyForId(uint id) const
{
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        if (id != 0 && it->id == id)
            return it.key();
    }
    return {};
}

void DesktopNotifier::onActionInvoked(uint id, const QString &action)
{
    // Signal diffusé à tous : les notifications des autres applications
    // (identifiants inconnus) sont ignorées.
    const QString key = keyForId(id);
    if (!key.isEmpty())
        emit actionInvoked(key, action, m_tokens.take(id));
}

void DesktopNotifier::onActivationToken(uint id, const QString &token)
{
    if (!keyForId(id).isEmpty())
        m_tokens.insert(id, token);
}

void DesktopNotifier::onNotificationClosed(uint id, uint reason)
{
    Q_UNUSED(reason)
    m_tokens.remove(id);
    const QString key = keyForId(id);
    if (key.isEmpty())
        return;
    Entry &entry = m_entries[key];
    if (entry.pending) {
        // Fermée pendant qu'une mise à jour est en route : le serveur
        // l'affichera comme une nouvelle notification.
        entry.id = 0;
        return;
    }
    m_entries.remove(key);
    emit closed(key);
}
