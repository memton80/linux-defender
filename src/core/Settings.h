#pragma once

#include "ScanJob.h"

#include <QDateTime>
#include <QString>
#include <QStringList>

// Réglages de l'application, enregistrés par QSettings dans
// ~/.config/linux-defender/linux-defender.conf.
namespace Settings
{
// Valeurs par défaut, partagées avec le bouton « Valeurs par défaut » des paramètres.
namespace Defaults
{
constexpr int checkInterval = 30;      // secondes
constexpr int signaturesMaxAge = 3;    // jours
constexpr bool scanHidden = true;
constexpr int maxFileSizeMb = 0;       // pas de limite
constexpr bool usbAutoScan = true;
constexpr bool usbNotify = true;
constexpr bool notifyScanFinished = true;
constexpr bool notifyRealtime = true;
constexpr bool notifyClamdLost = true;
constexpr bool notifySignatures = true;
constexpr bool closeToTray = true;
constexpr int historyMaxEntries = 100;
constexpr bool quickScanSystemAreas = true;
constexpr int scheduleFrequency = 0; // ScanSchedule::Frequency::Never
constexpr int scheduleKind = 0;      // ScanSchedule::Kind::Quick
constexpr bool scheduleSkipOnBattery = true;
// Téléchargements, Bureau et Documents (ceux qui existent), sinon le dossier personnel.
QStringList quickScanPaths();
}

// --- clamd ---

// Chemin du socket saisi par l'utilisateur ; vide = détection automatique.
QString socketPath();
void setSocketPath(const QString &path);

// Chemin réellement utilisé : celui des réglages, sinon celui détecté.
QString effectiveSocketPath();

// Intervalle entre deux vérifications de l'état de clamd, en secondes.
int checkInterval();
void setCheckInterval(int seconds);

// Âge au-delà duquel les signatures sont signalées comme obsolètes, en jours ;
// 0 = jamais.
int signaturesMaxAge();
void setSignaturesMaxAge(int days);

// --- Analyse ---

// Dossiers de l'analyse rapide. Liste vide dans les réglages = Defaults::quickScanPaths().
QStringList quickScanPaths();
void setQuickScanPaths(const QStringList &paths);

// Analyse rapide : aussi les emplacements sensibles et les programmes en cours
// (voir ScanOptions::systemAreas).
bool quickScanSystemAreas();
void setQuickScanSystemAreas(bool enabled);

// Dossiers et fichiers exclus des analyses.
QStringList excludedPaths();
void setExcludedPaths(const QStringList &paths);

// Analyse des fichiers et dossiers cachés.
bool scanHidden();
void setScanHidden(bool enabled);

// Taille maximale des fichiers analysés dans un dossier, en Mo ; 0 = pas de limite.
int maxFileSizeMb();
void setMaxFileSizeMb(int megabytes);

// Réglages précédents, sous la forme attendue par ScanManager.
ScanOptions scanOptions();

// --- Clés USB ---

// Scan automatique des clés USB au montage.
bool usbAutoScan();
void setUsbAutoScan(bool enabled);

// Notification au début et à la fin du scan d'une clé.
bool usbNotify();
void setUsbNotify(bool enabled);

// --- Notifications (les menaces trouvées par un scan sont toujours notifiées) ---

// Fin d'un scan lancé par l'utilisateur, sans menace, fenêtre en arrière-plan.
bool notifyScanFinished();
void setNotifyScanFinished(bool enabled);

// Détections de la protection en temps réel.
bool notifyRealtime();
void setNotifyRealtime(bool enabled);

// clamd ne répond plus.
bool notifyClamdLost();
void setNotifyClamdLost(bool enabled);

// Signatures plus anciennes que signaturesMaxAge().
bool notifySignatures();
void setNotifySignatures(bool enabled);

// --- Général ---

// Fermer la fenêtre la masque (l'application reste dans la zone de
// notification) ; sinon, fermer la fenêtre quitte l'application.
bool closeToTray();
void setCloseToTray(bool enabled);

// Nombre d'analyses gardées dans l'historique ; 0 = pas d'historique.
int historyMaxEntries();
void setHistoryMaxEntries(int count);

// --- Analyses planifiées (voir ScanSchedule) ---

// ScanSchedule::Frequency et ScanSchedule::Kind, en entier.
int scheduleFrequency();
void setScheduleFrequency(int frequency);
int scheduleKind();
void setScheduleKind(int kind);
// Reportée tant que l'ordinateur est sur batterie.
bool scheduleSkipOnBattery();
void setScheduleSkipOnBattery(bool enabled);
// Fin de la dernière analyse planifiée (invalide : jamais).
QDateTime scheduleLastRun();
void setScheduleLastRun(const QDateTime &time);
}
