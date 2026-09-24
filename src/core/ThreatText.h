#pragma once

#include <QDir>
#include <QList>
#include <QString>

/**
 * Textes des alertes : nom de menace lisible, chemin raccourci, titre et
 * corps des notifications. Le nom exact donné par ClamAV reste affiché dans
 * la fenêtre ; la notification, elle, doit se lire d'un coup d'œil.
 */
namespace ThreatText
{

// Une menace détectée : fichier et nom ClamAV (« Win.Test.EICAR_HDB-1 »).
struct Threat
{
    QString path;
    QString name;
};

struct Alert
{
    QString title;
    QString body;
};

// Nom lisible d'après la convention de nommage de ClamAV
// (Plateforme.Catégorie.Nom-Id) : « Win.Trojan.Agent-123-0 » devient
// « Cheval de Troie (Windows) ». Nom non reconnu : renvoyé tel quel.
QString describe(const QString &signature);

// « /home/alex/Téléchargements » devient « ~/Téléchargements » ; au-delà de
// maxLength caractères, le milieu est remplacé par « … ».
QString shortPath(const QString &path, const QString &home = QDir::homePath(), int maxLength = 50);

// Alerte des détections en temps réel pas encore consultées (une ou plusieurs).
Alert realtimeAlert(const QList<Threat> &threats, const QString &home = QDir::homePath());

// Alerte de fin de scan : les premières menaces, leur nombre total, et le
// bilan du scan.
Alert scanAlert(const QList<Threat> &firstThreats, qint64 total, const QString &summary);

} // namespace ThreatText
