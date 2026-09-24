#pragma once

#include <QDBusConnection>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>

#include <optional>

/**
 * Notifications du bureau, envoyées directement au service
 * org.freedesktop.Notifications (Plasma, GNOME et la plupart des bureaux).
 *
 * QSystemTrayIcon::showMessage() ne sait afficher qu'un titre et un texte.
 * Ici : boutons d'action, urgence (une alerte critique reste affichée, même
 * en mode « Ne pas déranger »), rattachement au fichier .desktop de
 * l'application (nom, icône, réglages dans Configuration du système), et mise
 * à jour d'une notification déjà affichée plutôt qu'une nouvelle à chaque fois.
 *
 * Chaque notification est désignée par une clé choisie par l'appelant : une
 * nouvelle notification avec la même clé remplace celle qui est affichée.
 */
class DesktopNotifier : public QObject
{
    Q_OBJECT

public:
    enum class Urgency { Low = 0, Normal = 1, Critical = 2 };

    struct Notification
    {
        QString title;
        QString body;  // texte simple (échappé si le serveur interprète le HTML)
        QString icon;  // nom d'icône du thème ou chemin absolu d'un fichier
        Urgency urgency = Urgency::Normal;
        QList<QPair<QString, QString>> actions; // (clé, libellé) ; « default » : clic sur la notification
        int timeoutMsecs = -1;                   // -1 : choix du serveur ; 0 : jusqu'à fermeture
    };

    static constexpr const char *kService = "org.freedesktop.Notifications";

    DesktopNotifier(const QString &appName, const QString &desktopEntry,
                    const QDBusConnection &bus = QDBusConnection::sessionBus(), QObject *parent = nullptr);

    // false si aucun service de notification n'est disponible : l'appelant
    // se rabat alors sur autre chose (QSystemTrayIcon::showMessage).
    bool show(const QString &key, const Notification &notification);
    void close(const QString &key);

signals:
    // Bouton (ou clic, action « default ») sur la notification de clé `key`.
    // activationToken : jeton xdg-activation fourni par le serveur, pour que
    // la fenêtre ouverte en réponse puisse prendre le focus sous Wayland.
    void actionInvoked(const QString &key, const QString &action, const QString &activationToken);
    // Notification fermée par l'utilisateur ou par le serveur (pas après close()).
    void closed(const QString &key);
    // Le serveur a refusé la notification : l'appelant peut se rabattre.
    void failed(const QString &key, const DesktopNotifier::Notification &notification);

private slots:
    void onActionInvoked(uint id, const QString &action);
    void onNotificationClosed(uint id, uint reason);
    void onActivationToken(uint id, const QString &token);

private:
    // Notification affichée (ou en cours d'envoi) pour une clé.
    struct Entry
    {
        uint id = 0;          // identifiant donné par le serveur (0 : pas encore connu)
        bool pending = false; // Notify() envoyé, réponse attendue
        bool closeRequested = false;
        std::optional<Notification> queued; // à envoyer dès que l'identifiant est connu
    };

    bool isAvailable() const;
    void send(const QString &key, const Notification &notification);
    void closeId(uint id);
    QString keyForId(uint id) const;

    QString m_appName;
    QString m_desktopEntry;
    QDBusConnection m_bus;
    bool m_bodyMarkup = true; // le serveur interprète-t-il le HTML ? (oui pour la plupart)
    QHash<QString, Entry> m_entries;
    QHash<uint, QString> m_tokens; // jetons d'activation reçus, par identifiant
};

Q_DECLARE_METATYPE(DesktopNotifier::Notification)
