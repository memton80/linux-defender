#include "SettingsDialog.h"

#include "core/ClamdClient.h"
#include "core/Settings.h"
#include "system/Autostart.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Paramètres"));

    // Vide = détection automatique ; le chemin détecté est affiché en indication.
    m_socketPath = new QLineEdit(Settings::socketPath());
    m_socketPath->setPlaceholderText(tr("Automatique : %1").arg(ClamdClient::detectSocketPath()));
    m_socketPath->setClearButtonEnabled(true);
    m_socketPath->setToolTip(tr("Laissez vide pour la détection automatique."));

    m_usbAutoScan = new QCheckBox(tr("Scanner automatiquement les clés USB et cartes mémoire au montage"));
    m_usbAutoScan->setChecked(Settings::usbAutoScan());

    m_autostart = new QCheckBox(tr("Lancer au démarrage de la session (dans la zone de notification)"));
    m_autostart->setChecked(Autostart::isEnabled());

    auto *form = new QFormLayout;
    form->addRow(tr("Socket de clamd :"), m_socketPath);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_usbAutoScan);
    layout->addWidget(m_autostart);
    layout->addStretch();
    layout->addWidget(buttons);
}

void SettingsDialog::accept()
{
    Settings::setSocketPath(m_socketPath->text().trimmed());
    Settings::setUsbAutoScan(m_usbAutoScan->isChecked());

    QString error;
    if (m_autostart->isChecked() != Autostart::isEnabled() && !Autostart::setEnabled(m_autostart->isChecked(), &error))
        QMessageBox::warning(this, tr("Démarrage automatique"), error);

    QDialog::accept();
}
