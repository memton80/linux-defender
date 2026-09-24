#pragma once

#include <QDialog>

class QCheckBox;
class QLineEdit;

/**
 * Petite boîte de dialogue « Paramètres » : chemin du socket de clamd, scan
 * automatique des clés USB, démarrage automatique à l'ouverture de session.
 *
 * Les réglages sont enregistrés quand on valide (bouton OK).
 */
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    void accept() override;

private:
    QLineEdit *m_socketPath;
    QCheckBox *m_usbAutoScan;
    QCheckBox *m_autostart;
};
