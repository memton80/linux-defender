#pragma once

#include <QDialog>

class ClamdClient;
class PathListEdit;
class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QStackedWidget;

/**
 * Boîte de dialogue « Paramètres », en pages : Général, Analyse, Clés USB,
 * Notifications, clamd.
 *
 * Les réglages sont enregistrés avec « OK » ou « Appliquer » (signal
 * applied()) ; « Valeurs par défaut » remplit toutes les pages avec les
 * valeurs par défaut, sans rien enregistrer.
 */
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    void accept() override;

signals:
    // Réglages enregistrés : l'application doit les prendre en compte.
    void applied();

private:
    QWidget *createGeneralPage();
    QWidget *createScanPage();
    QWidget *createUsbPage();
    QWidget *createNotificationsPage();
    QWidget *createClamdPage();
    void addPage(QWidget *content, const QString &name, const QString &description, const QIcon &icon);

    void load();
    void loadDefaults();
    void apply();
    void setModified(bool modified);
    void watchChanges();
    void updateDependentWidgets();
    void testConnection();

    QListWidget *m_pageList;
    QStackedWidget *m_pages;
    QDialogButtonBox *m_buttons;
    bool m_modified = false;

    // Général
    QCheckBox *m_autostart;
    QCheckBox *m_closeToTray;
    QSpinBox *m_historyMax;
    // Analyse
    PathListEdit *m_quickScanPaths;
    QCheckBox *m_quickScanSystemAreas;
    QComboBox *m_scheduleFrequency;
    QComboBox *m_scheduleKind;
    QCheckBox *m_scheduleSkipOnBattery;
    PathListEdit *m_excludedPaths;
    QCheckBox *m_scanHidden;
    QCheckBox *m_limitFileSize;
    QSpinBox *m_maxFileSize;
    // Clés USB
    QCheckBox *m_usbAutoScan;
    QCheckBox *m_usbNotify;
    // Notifications
    QCheckBox *m_notifyScanFinished;
    QCheckBox *m_notifyRealtime;
    QCheckBox *m_notifyClamdLost;
    QCheckBox *m_notifySignatures;
    // clamd
    QLineEdit *m_socketPath;
    QSpinBox *m_checkInterval;
    QSpinBox *m_signaturesMaxAge;
    QLabel *m_testIcon;
    QLabel *m_testResult;
    ClamdClient *m_testClient;
};
