#pragma once

#include <QString>
#include <QStringList>

/**
 * Lecture de la configuration de clamd (clamd.conf, ou scan.conf sous
 * Fedora) : socket, limites de taille, réglages que le diagnostic vérifie.
 *
 * Format : une directive par ligne, « Nom valeur » ; « # » commente la ligne.
 * Une directive absente vaut sa valeur par défaut (celles de ClamAV 1.x).
 */
struct ClamdConfig
{
    // Valeurs par défaut de clamd (ClamAV 1.x), vérifiées avec clamconf 1.5.4.
    static constexpr qint64 kDefaultMaxFileSize = 100 * 1024 * 1024;
    static constexpr qint64 kDefaultMaxScanSize = 400 * 1024 * 1024;
    // Au-delà de cette taille, clamd n'analyse aucun fichier, quelle que soit
    // sa configuration (même avec MaxFileSize 0), et répond pourtant « OK ».
    // Mesuré avec ClamAV 1.5.4 : 2 147 483 645 octets analysés, pas un de plus.
    static constexpr qint64 kEngineMaxFileSize = 2147483645;

    QString path;               // fichier lu ; vide si aucun n'a pu l'être
    QString localSocket;        // directive LocalSocket ; vide si absente
    qint64 maxFileSize = kDefaultMaxFileSize; // 0 = pas de limite (sauf celle du moteur)
    qint64 maxScanSize = kDefaultMaxScanSize; // données analysées par fichier (archives...)
    bool alertExceedsMax = false; // signaler (Heuristics.Limits.Exceeded) ce qui dépasse une limite
    bool exampleLine = false;     // ligne « Example » active : clamd refuse de démarrer

    // Taille au-delà de laquelle clamd répond « OK » sans avoir lu le fichier.
    qint64 unscannedAbove() const;

    // Fichiers de configuration de clamd selon les distributions, dans l'ordre de recherche.
    static QStringList defaultFiles();

    // Analyse le contenu d'un fichier de configuration.
    static ClamdConfig parse(const QString &content);
    // Lit un fichier ; s'il est illisible, `path` est vide (valeurs par défaut).
    static ClamdConfig read(const QString &path);
    // Configuration du clamd qui écoute sur `socketPath` : le fichier dont la
    // directive LocalSocket est ce socket, sinon le premier fichier lisible.
    // Aucun fichier lisible : valeurs par défaut, `path` vide.
    static ClamdConfig forSocket(const QString &socketPath, const QStringList &files = defaultFiles());

    // Taille au format de clamd : « 100M », « 1g », « 512K », « 1048576 ».
    // Renvoie -1 si la valeur n'est pas reconnue.
    static qint64 parseSize(const QString &value);
};
