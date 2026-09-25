#include "ThreatText.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QStringList>

#include <algorithm>

namespace
{
constexpr int kMaxListedThreats = 3; // au-delà : « … et N autres »
constexpr int kMaxFileNameLength = 40;

QString tr(const char *text)
{
    return QCoreApplication::translate("ThreatText", text);
}

// En français, 0 et 1 sont au singulier.
QString plural(qint64 count, const char *singular, const char *pluralForm)
{
    return tr(count > 1 ? pluralForm : singular).arg(QLocale().toString(count));
}

QString elideMiddle(const QString &text, int maxLength)
{
    if (text.size() <= maxLength)
        return text;
    const int kept = maxLength - 1; // place du « … »
    const int left = (kept + 1) / 2;
    return text.left(left) + QChar(0x2026) + text.right(kept - left);
}

// Catégories de la convention de nommage de ClamAV.
QString categoryName(const QString &category)
{
    static const QHash<QString, const char *> names = {
        {QStringLiteral("Adware"), QT_TRANSLATE_NOOP("ThreatText", "Logiciel publicitaire")},
        {QStringLiteral("Backdoor"), QT_TRANSLATE_NOOP("ThreatText", "Porte dérobée")},
        {QStringLiteral("Coinminer"), QT_TRANSLATE_NOOP("ThreatText", "Mineur de cryptomonnaie")},
        {QStringLiteral("Downloader"), QT_TRANSLATE_NOOP("ThreatText", "Téléchargeur malveillant")},
        {QStringLiteral("Dropper"), QT_TRANSLATE_NOOP("ThreatText", "Installeur malveillant")},
        {QStringLiteral("Exploit"), QT_TRANSLATE_NOOP("ThreatText", "Exploitation de faille")},
        {QStringLiteral("Infostealer"), QT_TRANSLATE_NOOP("ThreatText", "Voleur de données")},
        {QStringLiteral("Ircbot"), QT_TRANSLATE_NOOP("ThreatText", "Robot IRC malveillant")},
        {QStringLiteral("Joke"), QT_TRANSLATE_NOOP("ThreatText", "Programme farceur")},
        {QStringLiteral("Keylogger"), QT_TRANSLATE_NOOP("ThreatText", "Enregistreur de frappe")},
        {QStringLiteral("Loader"), QT_TRANSLATE_NOOP("ThreatText", "Chargeur malveillant")},
        {QStringLiteral("Macro"), QT_TRANSLATE_NOOP("ThreatText", "Macro malveillante")},
        {QStringLiteral("Malware"), QT_TRANSLATE_NOOP("ThreatText", "Logiciel malveillant")},
        {QStringLiteral("Packed"), QT_TRANSLATE_NOOP("ThreatText", "Programme compressé suspect")},
        {QStringLiteral("Packer"), QT_TRANSLATE_NOOP("ThreatText", "Programme compressé suspect")},
        {QStringLiteral("Phishing"), QT_TRANSLATE_NOOP("ThreatText", "Hameçonnage")},
        {QStringLiteral("Proxy"), QT_TRANSLATE_NOOP("ThreatText", "Relais malveillant")},
        {QStringLiteral("Ransomware"), QT_TRANSLATE_NOOP("ThreatText", "Rançongiciel")},
        {QStringLiteral("Rootkit"), QT_TRANSLATE_NOOP("ThreatText", "Rootkit")},
        {QStringLiteral("Spyware"), QT_TRANSLATE_NOOP("ThreatText", "Logiciel espion")},
        {QStringLiteral("Tool"), QT_TRANSLATE_NOOP("ThreatText", "Outil potentiellement dangereux")},
        {QStringLiteral("Trojan"), QT_TRANSLATE_NOOP("ThreatText", "Cheval de Troie")},
        {QStringLiteral("Virus"), QT_TRANSLATE_NOOP("ThreatText", "Virus")},
        {QStringLiteral("Worm"), QT_TRANSLATE_NOOP("ThreatText", "Ver informatique")},
    };
    const char *name = names.value(category);
    return name ? tr(name) : QString();
}

// Plateformes de la convention de nommage de ClamAV (inconnue : chaîne vide).
QString platformName(const QString &platform)
{
    static const QHash<QString, const char *> names = {
        {QStringLiteral("Andr"), QT_TRANSLATE_NOOP("ThreatText", "Android")},
        {QStringLiteral("Asp"), QT_TRANSLATE_NOOP("ThreatText", "ASP")},
        {QStringLiteral("Doc"), QT_TRANSLATE_NOOP("ThreatText", "document Word")},
        {QStringLiteral("Email"), QT_TRANSLATE_NOOP("ThreatText", "e-mail")},
        {QStringLiteral("Html"), QT_TRANSLATE_NOOP("ThreatText", "page web")},
        {QStringLiteral("Img"), QT_TRANSLATE_NOOP("ThreatText", "image")},
        {QStringLiteral("Ios"), QT_TRANSLATE_NOOP("ThreatText", "iOS")},
        {QStringLiteral("Java"), QT_TRANSLATE_NOOP("ThreatText", "Java")},
        {QStringLiteral("Js"), QT_TRANSLATE_NOOP("ThreatText", "JavaScript")},
        {QStringLiteral("Lnk"), QT_TRANSLATE_NOOP("ThreatText", "raccourci Windows")},
        {QStringLiteral("Mbr"), QT_TRANSLATE_NOOP("ThreatText", "secteur de démarrage")},
        {QStringLiteral("Multios"), QT_TRANSLATE_NOOP("ThreatText", "multiplateforme")},
        {QStringLiteral("Onenote"), QT_TRANSLATE_NOOP("ThreatText", "OneNote")},
        {QStringLiteral("Osx"), QT_TRANSLATE_NOOP("ThreatText", "macOS")},
        {QStringLiteral("Pdf"), QT_TRANSLATE_NOOP("ThreatText", "PDF")},
        {QStringLiteral("Php"), QT_TRANSLATE_NOOP("ThreatText", "PHP")},
        {QStringLiteral("Ppt"), QT_TRANSLATE_NOOP("ThreatText", "présentation PowerPoint")},
        {QStringLiteral("Rtf"), QT_TRANSLATE_NOOP("ThreatText", "document RTF")},
        {QStringLiteral("Swf"), QT_TRANSLATE_NOOP("ThreatText", "Flash")},
        {QStringLiteral("Txt"), QT_TRANSLATE_NOOP("ThreatText", "texte")},
        {QStringLiteral("Unix"), QT_TRANSLATE_NOOP("ThreatText", "Linux/Unix")},
        {QStringLiteral("Vba"), QT_TRANSLATE_NOOP("ThreatText", "macro Office")},
        {QStringLiteral("Win"), QT_TRANSLATE_NOOP("ThreatText", "Windows")},
        {QStringLiteral("Xls"), QT_TRANSLATE_NOOP("ThreatText", "classeur Excel")},
        {QStringLiteral("Xml"), QT_TRANSLATE_NOOP("ThreatText", "XML")},
    };
    const char *name = names.value(platform);
    return name ? tr(name) : QString();
}

// Détections heuristiques : « Heuristics.<Type>... ».
QString heuristicName(const QString &type)
{
    if (type == QLatin1String("Phishing"))
        return tr("Hameçonnage probable (détection heuristique)");
    if (type == QLatin1String("Encrypted"))
        return tr("Fichier chiffré, impossible à analyser");
    if (type == QLatin1String("Limits"))
        return tr("Fichier trop volumineux ou trop complexe pour être analysé");
    if (type == QLatin1String("Broken"))
        return tr("Fichier exécutable malformé");
    if (type == QLatin1String("Structured"))
        return tr("Données personnelles sensibles");
    if (type == QLatin1String("OLE2"))
        return tr("Document contenant des macros");
    return tr("Fichier suspect (détection heuristique)");
}

QString fileName(const QString &path)
{
    return elideMiddle(QFileInfo(path).fileName(), kMaxFileNameLength);
}

// « fichier : description » pour les premières menaces, puis « … et N autres ».
QStringList threatLines(const QList<ThreatText::Threat> &threats, qint64 total)
{
    QStringList lines;
    for (qsizetype i = 0; i < threats.size() && i < kMaxListedThreats; ++i)
        lines << tr("%1 : %2").arg(fileName(threats[i].path), ThreatText::describe(threats[i].name));
    const qint64 others = total - lines.size();
    if (others > 0)
        lines << plural(others, "… et %1 autre", "… et %1 autres");
    return lines;
}
}

namespace ThreatText
{

Kind kind(const QString &signature)
{
    QString name = signature.trimmed();
    if (name.endsWith(QLatin1String(".UNOFFICIAL")))
        name.chop(11);
    if (name.startsWith(QLatin1String("Heuristics.Encrypted."))
        || name.startsWith(QLatin1String("Heuristics.Limits.Exceeded.")))
        return Kind::Unscanned;
    if (name.startsWith(QLatin1String("Heuristics.")) || name.startsWith(QLatin1String("PUA.")))
        return Kind::Suspicious;
    return Kind::Threat;
}

QString describe(const QString &signature)
{
    QString name = signature.trimmed();
    if (name.endsWith(QLatin1String(".UNOFFICIAL"))) // signature d'une base tierce
        name.chop(11);

    // Fichier de test EICAR, reconnu par tous les antivirus : inoffensif.
    if (name.contains(QLatin1String("eicar"), Qt::CaseInsensitive))
        return tr("Fichier de test EICAR (inoffensif)");

    const QStringList parts = name.split(QLatin1Char('.'));
    if (parts.size() >= 2 && parts[0] == QLatin1String("Heuristics"))
        return heuristicName(parts[1]);

    // « PUA.Win.Adware.Nom » : programme potentiellement indésirable.
    const bool pua = parts.size() >= 4 && parts[0] == QLatin1String("PUA");
    const qsizetype first = pua ? 1 : 0;
    if (parts.size() - first < 3) // Plateforme.Catégorie.Nom attendus
        return signature;

    const QString category = parts[first + 1];
    if (category == QLatin1String("Test"))
        return tr("Fichier de test (inoffensif)");
    const QString what = pua ? tr("Programme potentiellement indésirable") : categoryName(category);
    if (what.isEmpty())
        return signature;
    const QString platform = platformName(parts[first]);
    return platform.isEmpty() ? what : tr("%1 (%2)").arg(what, platform);
}

QString shortPath(const QString &path, const QString &home, int maxLength)
{
    QString result = QDir::cleanPath(path);
    const QString homeDir = QDir::cleanPath(home);
    if (!home.isEmpty() && homeDir != QLatin1String("/")
        && (result == homeDir || result.startsWith(homeDir + QLatin1Char('/'))))
        result = QLatin1Char('~') + result.mid(homeDir.size());
    if (result.size() <= maxLength)
        return result;

    // Garde le début (« ~/.var ») et autant de dossiers de fin que possible :
    // « ~/.var/…/cache/safebrowsing-backup ».
    const QStringList parts = result.split(QLatin1Char('/'));
    if (parts.size() > 3) {
        const QString head = parts[0] + QLatin1Char('/') + parts[1] + QLatin1Char('/') + QChar(0x2026) + QLatin1Char('/');
        QString tail = parts.last();
        for (qsizetype i = parts.size() - 2; i > 1; --i) {
            if (head.size() + parts[i].size() + 1 + tail.size() > maxLength)
                break;
            tail = parts[i] + QLatin1Char('/') + tail;
        }
        result = head + tail;
    }
    return elideMiddle(result, maxLength);
}

Alert realtimeAlert(const QList<Threat> &threats, const QString &home)
{
    if (threats.isEmpty())
        return {};
    const bool onlySuspicious = std::all_of(threats.cbegin(), threats.cend(), [](const Threat &threat) {
        return kind(threat.name) == Kind::Suspicious;
    });
    if (threats.size() == 1) {
        const Threat &threat = threats.first();
        return {(onlySuspicious ? tr("Fichier suspect : %1") : tr("Menace détectée : %1")).arg(fileName(threat.path)),
                tr("%1\nDans %2, toujours en place.")
                    .arg(describe(threat.name), shortPath(QFileInfo(threat.path).absolutePath(), home))};
    }
    QStringList lines = threatLines(threats, threats.size());
    lines << tr("Aucun fichier n'a été supprimé ni déplacé.");
    return {onlySuspicious ? plural(threats.size(), "%1 fichier suspect détecté", "%1 fichiers suspects détectés")
                           : plural(threats.size(), "%1 menace détectée", "%1 menaces détectées"),
            lines.join(QLatin1Char('\n'))};
}

Alert scanAlert(const QList<Threat> &firstThreats, qint64 total, const QString &summary)
{
    const QStringList lines = QStringList{summary} + threatLines(firstThreats, total);
    return {plural(total, "%1 menace détectée par le scan", "%1 menaces détectées par le scan"),
            lines.join(QLatin1Char('\n'))};
}

Alert suspiciousAlert(const QList<Threat> &firstSuspicious, qint64 total, const QString &summary)
{
    const QStringList lines = QStringList{summary} + threatLines(firstSuspicious, total);
    return {plural(total, "%1 fichier suspect trouvé par l'analyse", "%1 fichiers suspects trouvés par l'analyse"),
            lines.join(QLatin1Char('\n'))};
}

} // namespace ThreatText
