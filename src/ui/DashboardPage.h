#pragma once

#include "StatusDisplay.h"
#include "core/ScanManager.h"

#include <QTimer>
#include <QWidget>

class Card;
class ClamdWatcher;
class OnAccessController;
class QCommandLinkButton;
class QLabel;
class QProgressBar;
class QPushButton;
class ScanHistory;

/**
 * Page « Accueil » : bandeau qui résume l'état de la protection (et propose
 * l'action la plus utile), tuiles d'état (clamd, signatures, temps réel,
 * dernière analyse, clés USB, historique) et lancement des analyses.
 *
 * Elle ne fait rien elle-même : elle observe les autres objets et émet des
 * signaux, reliés par la fenêtre principale.
 */
class DashboardPage : public QWidget
{
    Q_OBJECT

public:
    DashboardPage(ClamdWatcher *watcher, ScanManager *scans, OnAccessController *onAccess, ScanHistory *history,
                  QWidget *parent = nullptr);

    // Gravité de l'état résumé par le bandeau (icône de la page dans la fenêtre).
    StatusDisplay::Level level() const;
    // Détections en temps réel depuis le lancement, pas encore effacées de la liste.
    void setRealtimeThreats(int count);
    // Relit tous les états (après une modification des paramètres, par exemple).
    void refresh();

signals:
    void levelChanged();
    void checkRequested();
    void quickScanRequested();
    void fullScanRequested();
    void folderScanRequested();
    void filesScanRequested();
    void showScanRequested();
    void showOnAccessRequested();
    void showHistoryRequested();
    void settingsRequested();

protected:
    void showEvent(QShowEvent *event) override;

private:
    // Action du bouton du bandeau, selon l'état.
    enum class BannerAction { None, Check, QuickScan, ShowOnAccess, ShowHistory };

    struct Tile
    {
        Card *card = nullptr;
        QLabel *icon = nullptr;
        QLabel *value = nullptr;
        QLabel *detail = nullptr;
    };

    Tile makeTile(const QString &caption, const QString &buttonText, void (DashboardPage::*onClick)());
    void setTile(const Tile &tile, const QIcon &icon, const QString &value, const QString &detail);
    void updateBanner();
    void updateTiles();
    void updateScanCard();

    ClamdWatcher *m_watcher;
    ScanManager *m_scans;
    OnAccessController *m_onAccess;
    ScanHistory *m_history;
    int m_realtimeThreats = 0;
    StatusDisplay::Level m_level = StatusDisplay::Level::Neutral;
    BannerAction m_bannerAction = BannerAction::None;

    Card *m_banner;
    QLabel *m_bannerIcon;
    QLabel *m_bannerTitle;
    QLabel *m_bannerText;
    QPushButton *m_bannerButton;

    Card *m_scanCard;
    QLabel *m_scanTitle;
    QProgressBar *m_scanProgress;

    Tile m_clamdTile;
    Tile m_signaturesTile;
    Tile m_onAccessTile;
    Tile m_lastScanTile;
    Tile m_usbTile;
    Tile m_historyTile;

    QCommandLinkButton *m_quickButton;
    QCommandLinkButton *m_fullButton;
    QCommandLinkButton *m_folderButton;
    QCommandLinkButton *m_filesButton;

    QTimer m_clock; // textes relatifs (« il y a 5 minutes ») à jour
};
