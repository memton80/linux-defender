# linux-defender

[![CI](https://github.com/memton80/linux-defender/actions/workflows/ci.yml/badge.svg)](https://github.com/memton80/linux-defender/actions/workflows/ci.yml)

Interface graphique native et légère pour l'antivirus [ClamAV](https://www.clamav.net/),
pensée pour KDE Plasma (thème Breeze clair/sombre automatique).
Écrite en C++ / Qt 6 (QtWidgets). Elle communique directement avec le démon `clamd`
par son socket Unix, avec le protocole natif de clamd.

Fonctions : tableau de bord de l'état de la protection, analyses rapide, complète ou à la demande
de fichiers et de dossiers (avec exclusions), analyse automatique des clés USB au montage,
historique des analyses, quarantaine, supervision de la protection en temps réel, diagnostic de
l'installation avec corrections en un clic, icône dans la zone de notification avec l'état de
clamd et des notifications, démarrage automatique à l'ouverture de session.

## Installation

Les paquets de chaque version sont sur la page des releases :
**<https://github.com/memton80/linux-defender/releases>**.

Version actuelle : **1.0.2**. Chaque release contient trois fichiers (remplacez `X.Y.Z` par la
version, par exemple `1.0.2`) :

| Fichier | Pour |
|---|---|
| `linux-defender-X.Y.Z-1.fc44.x86_64.rpm` | Fedora 44 |
| `linux-defender_X.Y.Z_amd64.deb` | Ubuntu 24.04 et suivantes |
| `linux-defender-X.Y.Z-linux-x86_64.tar.gz` | toute distribution avec Qt 6.4 ou plus récent |

### Fedora

```sh
sudo dnf install ./linux-defender-X.Y.Z-1.fc44.x86_64.rpm
```

### Debian / Ubuntu

```sh
sudo apt install ./linux-defender_X.Y.Z_amd64.deb
```

Les paquets recommandent clamd (`clamd` et `clamav-update` sous Fedora, `clamav-daemon` sous
Debian/Ubuntu) sans l'exiger : le socket étant configurable, clamd peut tourner ailleurs. Pour ne
pas l'installer, ajoutez `--setopt=install_weak_deps=False` (dnf) ou `--no-install-recommends` (apt).
Il faut ensuite configurer clamd : voir [Configurer clamd](#configurer-clamd).

Désinstallation : `sudo dnf remove linux-defender` ou `sudo apt remove linux-defender`.

### Archive .tar.gz (sans paquet)

L'archive contient le binaire et ses fichiers d'intégration au bureau, organisés comme un
préfixe d'installation (`bin/`, `share/`). Elle a besoin de Qt 6.4 ou plus récent (modules
de base et plugin SVG) déjà installé sur le système. Elle ne contient pas le service de
protection en temps réel, fourni seulement par les paquets.

```sh
tar -xzf linux-defender-X.Y.Z-linux-x86_64.tar.gz
./linux-defender-X.Y.Z-linux-x86_64/bin/linux-defender

# Ou installation pour votre utilisateur seulement (menu des applications compris) :
cp -r linux-defender-X.Y.Z-linux-x86_64/bin linux-defender-X.Y.Z-linux-x86_64/share ~/.local/
```

### Versions de développement

Chaque push sur `main` produit aussi les trois fichiers, avec la version du fichier `VERSION` : onglet
[Actions](https://github.com/memton80/linux-defender/actions/workflows/ci.yml), choisir un run, section « Artifacts »
(connexion à GitHub nécessaire).

## Configurer clamd

La page **Diagnostic** de l'application détecte chacun des points ci-dessous, donne la commande
exacte et, avec les paquets `.deb` et `.rpm`, applique la correction en un clic (voir
[Diagnostic](#diagnostic)). Les commandes restent utiles sans l'interface.

### Fedora

Sur Fedora, clamd tourne via le service `clamd@scan`, et son socket est désactivé par défaut.

```sh
sudo freshclam                                   # première mise à jour des signatures
sudo systemctl enable --now clamav-freshclam     # mises à jour automatiques
sudoedit /etc/clamd.d/scan.conf
```

Dans `/etc/clamd.d/scan.conf`, vérifiez que la ligne `Example` est commentée.
Décommentez aussi la ligne suivante :

```
LocalSocket /run/clamd.scan/clamd.sock
```

Puis démarrez le service :

```sh
sudo systemctl enable --now clamd@scan
```

### Debian / Ubuntu

Le service `clamav-daemon` démarre après l'installation. Il crée son socket dans
`/run/clamav/clamd.ctl`. Attendez que `clamav-freshclam` ait téléchargé les signatures.

### Droits d'accès au socket

Si l'application affiche « Permission refusée », votre utilisateur doit rejoindre le groupe
propriétaire du socket. L'application indique le groupe exact à rejoindre, en général
`virusgroup` sur Fedora et `clamav` sur Debian/Ubuntu :

```sh
sudo usermod -aG <groupe> $USER
```

Fermez ensuite votre session et rouvrez-la pour que le changement prenne effet.

### Fedora : autoriser clamd à analyser vos fichiers (SELinux)

Pour scanner, l'application ouvre elle-même chaque fichier puis transmet le fichier ouvert à
clamd (commande `FILDES`). Sur Fedora, SELinux peut interdire à clamd de lire des fichiers
de votre dossier personnel ou d'une clé USB, même transmis de cette façon. Si tous les
fichiers d'un scan sont en erreur (« No file descriptor received », « Permission denied »...),
autorisez clamd à lire les fichiers du système :

```sh
sudo setsebool -P antivirus_can_scan_system 1
```

## Utilisation

« Linux Defender » est dans le menu des applications, ou en ligne de commande :

```sh
linux-defender                           # ouvre la fenêtre, socket détecté automatiquement
linux-defender --background              # démarre directement dans la zone de notification
linux-defender --socket /chemin/clamd.sock
linux-defender --scan fichier.zip ~/Téléchargements   # analyse ces fichiers et dossiers
linux-defender --quick-scan               # lance une analyse rapide
```

`--scan` et `--quick-scan` passent par l'instance déjà lancée s'il y en a une (sinon, elle
démarre) : la fenêtre s'ouvre sur la page « Analyse ». Une analyse déjà en cours n'est pas
interrompue, la nouvelle attend son tour. Les chemins relatifs le sont au dossier de lancement.

Depuis une compilation des sources, le binaire est `build/bin/linux-defender`. Aide complète :
`man linux-defender`.

### Fenêtre

La fenêtre a une barre latérale avec six pages ; l'icône de chaque page reflète son état
(bouclier vert, orange ou rouge, analyse en cours...) :

- **Accueil** : un bandeau résume l'état de la protection et propose l'action la plus utile
  (« Analyse rapide », « Voir les menaces », « Vérifier maintenant »...). En dessous, des tuiles
  détaillent le moteur (clamd), les signatures et leur âge, la protection en temps réel, la
  dernière analyse, les clés USB et l'historique, puis quatre boutons lancent une analyse.
- **Analyse** : lancement et progression (fichier en cours, compteurs, durée), bilan, et liste
  des fichiers analysés avec filtres (menaces, erreurs, sains), recherche et export CSV.
- **Protection en temps réel** : état de `clamonacc`, dossiers surveillés et détections.
- **Quarantaine** : les fichiers mis en quarantaine, à restaurer ou à supprimer définitivement
  (voir [Quarantaine](#quarantaine)).
- **Historique** : les analyses passées et, pour chacune, son bilan et ses menaces.
- **Diagnostic** : la configuration de clamd et de la protection, vérifiée point par point, avec
  les corrections (voir [Diagnostic](#diagnostic)).

Clic droit sur un fichier d'une liste : « Mettre en quarantaine » (menace ou fichier suspect
encore en place), « Afficher dans le gestionnaire de fichiers » (Dolphin s'ouvre sur le dossier,
fichier sélectionné), « Copier le chemin », « Copier le nom de la menace ». Double-clic : même
chose que « Afficher dans le gestionnaire de fichiers ».

Aucune feuille de style : les couleurs viennent du thème (Breeze clair ou sombre) et suivent
ses changements.

Fermer la fenêtre ne quitte pas l'application : elle reste active dans la zone de notification
(réglable dans les paramètres).
- Clic gauche sur l'icône : afficher ou masquer la fenêtre.
- Clic droit : état de clamd, « Vérifier l'état de clamd », « Analyse rapide »,
  « Analyser un dossier… », « Arrêter l'analyse » (pendant une analyse), « Ouvrir la fenêtre »,
  « Quitter ».

L'application ne se lance qu'une fois : la relancer réaffiche simplement la fenêtre existante.
Sous Wayland, le jeton d'activation du lanceur (`XDG_ACTIVATION_TOKEN`) est transmis à
l'instance déjà lancée, pour que sa fenêtre puisse prendre le focus.

### Depuis le gestionnaire de fichiers

- **Dolphin (KDE)** : clic droit sur des fichiers ou des dossiers, « Analyser avec Linux
  Defender ». Le menu est installé par les paquets et l'archive
  (`share/kio/servicemenus/linux-defender-scan.desktop`) ; il s'applique aux fichiers locaux.
  Avec l'archive `.tar.gz` copiée dans `~/.local`, Dolphin exige que ce fichier soit exécutable
  (il l'est dans l'archive).
- **Menu des applications** : clic droit sur « Linux Defender », « Analyse rapide ».
- **Fichiers (GNOME)** : pas de menu contextuel installé, mais un script le remplace. Créez
  `~/.local/share/nautilus/scripts/Analyser avec Linux Defender`, rendez-le exécutable
  (`chmod +x`), avec ce contenu ; il apparaît dans le menu « Scripts » du clic droit :

  ```sh
  #!/bin/sh
  exec linux-defender --scan "$@"
  ```


### Analyses

- **Analyse rapide** : les dossiers où arrivent les nouveaux fichiers, par défaut Téléchargements,
  Bureau et Documents (modifiables dans les paramètres), plus les emplacements sensibles et les
  programmes en cours (voir ci-dessous) ;
- **Analyse complète** : tout le dossier personnel ;
- **Dossier…** et **Fichiers…** : ce que vous choisissez ;
- **Analyse planifiée** : rapide ou complète, chaque jour ou chaque semaine (voir ci-dessous).

Les dossiers sont parcourus récursivement ; les liens symboliques ne sont pas suivis, et `/proc`,
`/sys` et `/dev` sont ignorés, ainsi que les exclusions des paramètres. Un fichier n'est analysé
qu'une fois, même si plusieurs des chemins choisis le contiennent. Chaque fichier est ouvert par
l'application avec vos droits, puis transmis à clamd (comme `clamdscan --fdpass`) : clamd peut
donc analyser votre dossier personnel et vos clés USB sans avoir le droit de les lire lui-même.

#### Emplacements sensibles et programmes en cours

Par défaut, l'analyse rapide vérifie aussi, en quelques secondes, là où un programme malveillant
s'installe pour se relancer ou dépose ses fichiers (seulement ceux qui existent) :

- démarrage automatique : `~/.config/autostart`, services systemd de l'utilisateur
  (`~/.config/systemd/user`) ;
- programmes et raccourcis de l'utilisateur : `~/.local/bin`, `~/.local/share/applications` ;
- scripts de démarrage du shell et de la session : `~/.bashrc`, `~/.bash_profile`, `~/.profile`,
  `~/.zshrc`, `~/.xprofile`... ;
- fichiers temporaires : `/tmp`, `/var/tmp`, `/dev/shm`. Ces dossiers sont partagés entre les
  utilisateurs : seuls vos fichiers y sont analysés, ceux des autres (illisibles) sont ignorés
  sans erreur. C'est vrai aussi quand vous analysez vous-même un de ces dossiers ;
- **le programme de chaque processus en cours** qui vous appartient, lu par `/proc/<pid>/exe` :
  même s'il a été supprimé du disque après son lancement (technique courante des programmes
  malveillants), il reste analysable, et son chemin s'affiche suivi de `(deleted)`. Un programme
  lancé plusieurs fois n'est analysé qu'une fois ; les processus protégés (`gpg-agent`,
  `ssh-agent`...) sont ignorés.

Réglage : Paramètres → Analyse → « Analyser aussi les emplacements sensibles et les programmes en
cours ».

#### Analyses planifiées

Paramètres → Analyse → « Analyse planifiée » : fréquence (jamais, chaque jour, chaque semaine),
analyse rapide ou complète, et report tant que l'ordinateur est sur batterie (UPower). Pas d'heure
fixe : Linux Defender, qui tourne en arrière-plan (lancement à l'ouverture de la session
conseillé), vérifie tous les quarts d'heure si une analyse est due, à partir de 5 minutes après
son lancement pour ne pas ralentir l'ouverture de la session. Une analyse manquée (ordinateur
éteint) est donc faite dès que possible. Elle attend aussi la fin d'une autre analyse et que clamd
réponde.

Une analyse est due 23 heures après la précédente (chaque jour) ou 7 jours moins une heure (chaque
semaine). Arrêtée par l'utilisateur, elle compte comme faite ; si elle échoue (clamd perdu), elle
reste due. La tuile « Dernière analyse » de l'accueil indique la prochaine. Son bilan est notifié
comme celui d'une analyse lancée à la main, et enregistré dans l'historique (« Analyse
planifiée »).

Chaque fichier reçoit l'un de ces statuts :

| Statut | Signification |
|---|---|
| **Infecté** | signature d'un programme malveillant (ou fichier de test EICAR) |
| **Suspect** | soupçon seulement : détection heuristique (`Heuristics.*` : hameçonnage, exécutable malformé, document à macros...) ou programme potentiellement indésirable (`PUA.*`) |
| **Non analysé** | clamd n'a pas pu lire le fichier en entier : archive chiffrée (`Heuristics.Encrypted.*`), limite de taille dépassée (voir ci-dessous) |
| **Erreur** | fichier illisible, erreur de clamd |
| **Sain** | analysé, rien trouvé |

La liste affiche les menaces en premier (avec le nom donné par clamd ; l'infobulle le traduit en
clair), puis les fichiers suspects et non analysés (filtre « Avertissements »), les erreurs et
les fichiers sains. Pour limiter la mémoire utilisée, seuls les 10 000 premiers fichiers sains
sont listés ; les compteurs incluent tous les fichiers.

Un fichier suspect est signalé par une notification (sans alerte critique) et colore le bandeau
de l'accueil en orange. Un fichier non analysé est seulement cité dans le bilan : un dossier de
téléchargements contient souvent des archives chiffrées ou des images disque.

Chaque analyse terminée est enregistrée dans l'historique
(`~/.local/share/linux-defender/history.json`, 100 analyses par défaut) avec ses 100 premières
menaces et ses 100 premiers avertissements.

#### Fichiers trop gros pour clamd

clamd a ses propres limites (directives de sa configuration, valeurs par défaut de ClamAV 1.x) et,
quand un fichier les dépasse, il répond « OK » **sans l'avoir analysé** — comme pour un fichier
sain. Comportements vérifiés avec ClamAV 1.5.4 :

| Cas | Réponse de clamd | Affiché par Linux Defender |
|---|---|---|
| fichier plus gros que `MaxFileSize` (100 Mo par défaut, **25 Mo dans la configuration installée par Debian et Ubuntu** ; la limite elle-même est encore analysée) | « OK », fichier non lu | **Non analysé** : l'application lit `MaxFileSize` dans la configuration de clamd et compare la taille |
| fichier de plus de 2 147 483 645 octets (2 Go), même avec `MaxFileSize 0` | « OK », fichier non lu | **Non analysé** |
| archive dont le contenu décompressé dépasse `MaxScanSize` (400 Mo par défaut, 100 Mo sous Debian et Ubuntu) | « OK » : seul le début est analysé | Sain — sauf avec `AlertExceedsMax yes` : **Non analysé** (`Heuristics.Limits.Exceeded.MaxScanSize`) |
| fichier d'une archive plus gros que `MaxFileSize` | « OK » : il est tronqué et seul son début est analysé, même avec `AlertExceedsMax yes` | Sain |

La configuration lue est celle du clamd utilisé : le fichier (`/etc/clamd.d/scan.conf`,
`/etc/clamav/clamd.conf` ou `/etc/clamd.conf`) dont la directive `LocalSocket` est le socket de
l'application, sinon le premier lisible, sinon les valeurs par défaut. La page « Analyse » des
paramètres affiche la limite trouvée. Pour que les archives analysées en partie soient aussi
signalées, ajoutez `AlertExceedsMax yes` à cette configuration, puis redémarrez clamd.

### Quarantaine

Un fichier détecté reste à sa place tant que vous ne décidez rien. Pour le neutraliser :

- clic droit sur la menace (page « Analyse », « Protection en temps réel » ou détail d'une analyse
  de l'historique) → **Mettre en quarantaine** ;
- page « Analyse », après une analyse : bouton **Mettre les N menaces en quarantaine** (fichiers
  infectés ; les fichiers seulement suspects, souvent légitimes, se traitent un par un) ;
- alerte d'une détection en temps réel : bouton **Mettre en quarantaine**.

Le fichier est alors retiré de son emplacement et gardé dans
`~/.local/share/linux-defender/quarantine` (dossier 0700, fichiers 0600), **rendu inerte** : son
contenu est brouillé, il ne peut être ni ouvert ni exécuté, et ni clamd ni la protection en temps
réel ne le détectent de nouveau (vérifié avec ClamAV 1.5.4). Sont gardés avec lui : son
emplacement d'origine, le nom de la menace, la date, sa taille, ses droits et son empreinte
SHA-256 (clic droit → « Copier l'empreinte SHA-256 », pour le chercher sur VirusTotal ou
MalwareBazaar sans l'envoyer).

La page **Quarantaine** liste ces fichiers :

- **Restaurer…** (après un avertissement) : le fichier revient à son emplacement, avec ses droits
  (sans setuid ni setgid), après vérification de son empreinte. Un fichier présent entre-temps
  n'est jamais écrasé : le fichier restauré prend alors le nom `nom (restauré).ext` ;
- **Supprimer définitivement…** et **Tout supprimer…** (après confirmation).

Limites : l'application agit avec vos droits, sans privilège. Un fichier qui ne vous appartient
pas, ou sur un support en lecture seule, ne peut pas être retiré : un message l'explique et rien
n'est modifié. Un lien symbolique n'est jamais suivi. Si le fichier change pendant la mise en
quarantaine, rien n'est fait. La détection que `clamonacc` produit en lisant le fichier à ce
moment-là n'est pas signalée.

### Diagnostic

La page **Diagnostic** vérifie l'installation et, pour chaque problème, explique la cause, affiche
la commande exacte (bouton « Copier ») et propose une correction en un clic :

| Vérification | Problème détecté | Correction en un clic |
|---|---|---|
| clamd | service arrêté ou en échec | démarre et active le service (`clamd@scan`, `clamav-daemon` ou `clamd`) |
| | Fedora : ligne `Example` active, `LocalSocket` commenté | commente `Example`, active `LocalSocket`, démarre clamd |
| | clamd absent | — (commande d'installation de la distribution) |
| Accès au socket | « Permission refusée » | ajoute l'utilisateur au groupe du socket (`clamav`, `virusgroup` ou `clamscan`) |
| | déjà dans le groupe, mais session ouverte avant | — (fermer la session et la rouvrir) |
| Signatures | obsolètes, ou aucune base chargée | active `clamav-freshclam` ; sinon, son journal ou la commande d'installation |
| SELinux | booléen `antivirus_can_scan_system` désactivé | `setsebool -P antivirus_can_scan_system 1` |
| Limites d'analyse | `AlertExceedsMax` absent (archives analysées en partie sans avertissement) | `AlertExceedsMax yes`, puis redémarre clamd |
| Temps réel | protection désactivée | l'active et la démarre |
| | limite inotify basse (moins de 131 072 dossiers) ou atteinte | la porte à 524 288 (`/etc/sysctl.d/90-linux-defender.conf`) |

Le diagnostic se met à jour tout seul (état de clamd, services systemd) ; « Vérifier à nouveau »
le relance. Un problème qu'il est seul à voir (SELinux, par exemple) s'affiche aussi dans le
bandeau de l'accueil, dont le bouton « Résoudre » mène à cette page.

Les corrections passent par un petit programme d'aide, `/usr/libexec/linux-defender-helper`
(paquets `.deb` et `.rpm` seulement), lancé par `pkexec` sous une action polkit dédiée
(`io.github.memton80.linux-defender.manage`) :

- l'agent polkit du bureau demande le **mot de passe administrateur**, retenu quelques minutes
  (plusieurs corrections à la suite n'en demandent qu'un) ; rien n'est possible depuis une
  session distante ou inactive ;
- l'application ne tourne jamais en root. Elle ne transmet au programme d'aide qu'un nom d'action
  de sa liste fermée : le programme d'aide trouve lui-même les fichiers et services concernés, et
  le groupe du socket est vérifié contre une liste fixe. L'utilisateur ajouté à un groupe est
  toujours celui qui a lancé `pkexec` ;
- avant de modifier la configuration de clamd, il en garde une copie
  (`<fichier>.linux-defender-orig`), une seule fois.

Sans programme d'aide (archive `.tar.gz`) ou sans `pkexec`, la page affiche seulement les
commandes à lancer dans un terminal.

### Clés USB

Quand une clé USB ou une carte mémoire est **montée**, elle est analysée automatiquement, avec une
notification au début et à la fin de l'analyse. Sous Plasma, une clé est montée quand vous
l'ouvrez (Dolphin, notification « Périphériques ») ou dès le branchement si le montage
automatique est activé (Configuration du système → Disques et caméras → Montage automatique des
périphériques).

Tant qu'une analyse lit la clé, elle ne peut pas être éjectée : arrêtez l'analyse depuis la
fenêtre ou le menu de l'icône (« Arrêter l'analyse ») si besoin.

### Paramètres

Bouton « Paramètres » de la barre latérale. « Appliquer » enregistre sans fermer ; « Valeurs par
défaut » remplit toutes les pages avec les valeurs d'origine (sans toucher au démarrage
automatique).

| Page | Réglage | Par défaut |
|---|---|---|
| Général | Lancer à l'ouverture de la session (`~/.config/autostart/linux-defender.desktop`, avec `--background`) | non |
| | Garder l'application dans la zone de notification quand la fenêtre est fermée (sinon, fermer quitte) | oui |
| | Nombre d'analyses conservées dans l'historique (0 : historique désactivé) | 100 |
| Analyse | Dossiers de l'analyse rapide | Téléchargements, Bureau, Documents |
| | Analyse rapide : aussi les emplacements sensibles et les programmes en cours | oui |
| | Analyse planifiée : fréquence, rapide ou complète, report sur batterie | jamais ; rapide ; oui |
| | Exclusions : dossiers et fichiers ignorés quand ils sont dans un dossier analysé | aucune |
| | Analyser les fichiers et dossiers cachés | oui |
| | Ignorer les fichiers de plus de N Mo (comptés à part dans le bilan) | non |
| Clés USB | Analyser automatiquement au montage | oui |
| | Notifier le début et la fin de l'analyse d'une clé | oui |
| Notifications | Fin d'une analyse sans menace (fenêtre pas au premier plan) | oui |
| | Menace détectée en temps réel ; clamd ne répond plus ; signatures obsolètes | oui |
| clamd | Socket, avec bouton « Tester » (vide : détection automatique) | automatique |
| | Intervalle de vérification de l'état de clamd | 30 s |
| | Âge au-delà duquel les signatures sont signalées comme obsolètes (0 : jamais) | 3 jours |

Les menaces trouvées par une analyse sont toujours notifiées. Les exclusions, la limite de
taille et les fichiers cachés portent sur le contenu des dossiers parcourus : un dossier ou un
fichier choisi explicitement est toujours analysé.

Les réglages sont enregistrés dans `~/.config/linux-defender/linux-defender.conf`.

L'état de clamd est vérifié à intervalle régulier, à l'ouverture de la fenêtre et sur demande.
La vérification utilise la commande `VERSION` de clamd, qui donne aussi la version du moteur
et la date des signatures.

Le socket utilisé est, dans l'ordre :
1. celui de l'option `--socket` ;
2. celui des paramètres ;
3. la directive `LocalSocket` de `/etc/clamd.d/scan.conf`, `/etc/clamav/clamd.conf` ou `/etc/clamd.conf` ;
4. sinon, les emplacements usuels `/run/clamd.scan/clamd.sock` (Fedora) et `/run/clamav/clamd.ctl` (Debian/Ubuntu).

## Protection en temps réel (clamonacc)

> Depuis la version 1.0.2, les paquets `.deb` et `.rpm` installent le service
> `linux-defender-onaccess.service` **activé et démarré** : la protection en temps réel
> fonctionne dès l'installation (voir [Activer ou désactiver la
> protection](#activer-ou-désactiver-la-protection)). La case « Activer la protection en temps
> réel » de la page **Protection en temps réel** l'active ou la désactive.

`clamonacc` est le programme de ClamAV qui surveille les fichiers en temps réel : le noyau
(fanotify et inotify) lui signale chaque fichier ouvert, créé, écrit ou renommé dans les dossiers
surveillés, et il le transmet à clamd pour analyse. Linux Defender ne refait pas ce travail : il **supervise** le
service `linux-defender-onaccess.service` qui lance `clamonacc`, et lit son journal pour vous
prévenir de chaque détection :

- alerte immédiate du bureau (voir [Alertes](#alertes)) ;
- icône de la zone de notification en alerte ;
- page **Protection en temps réel** de la fenêtre : état du service et liste des détections
  (distincte des résultats des scans manuels).

Les fichiers détectés restent en place : l'alerte et la page proposent de les mettre en
[quarantaine](#quarantaine).

### Alertes

Une détection affiche une notification du bureau (Plasma, GNOME...) :

- titre avec le nom du fichier (« Menace détectée : eicar.com »), nature de la menace en clair
  (« Cheval de Troie (Windows) », « Fichier de test EICAR (inoffensif) »...) et dossier, raccourci
  (`~/Téléchargements`). Le nom exact donné par ClamAV est dans la fenêtre ;
- alerte **critique** : elle reste affichée jusqu'à ce que vous la fermiez, même en mode « Ne pas
  déranger », et disparaît d'elle-même quand vous ouvrez la fenêtre ;
- boutons **Afficher les détails** (page **Protection en temps réel**), **Mettre en
  quarantaine** et **Ouvrir le dossier** (gestionnaire de fichiers, fichier sélectionné). Le
  résultat de la mise en quarantaine remplace l'alerte. Attention : Dolphin peut générer un aperçu
  des images, PDF ou vidéos du dossier, et donc ouvrir le fichier détecté ;
- plusieurs détections avant que vous ne l'ayez consultée (une archive décompressée, par
  exemple) : une seule alerte, mise à jour (« 5 menaces détectées »), plutôt qu'une par fichier ;
- icône du thème (bouclier rouge de Breeze sous Plasma), celle de l'application sinon.
- fichier seulement **suspect** (détection heuristique, programme potentiellement indésirable) :
  notification normale, qui ne reste pas affichée ; fichier **non analysé** (archive chiffrée,
  limite de taille dépassée) : seulement listé dans la page, sans notification.

Pas d'aperçu du fichier dans l'alerte : pour le générer, le bureau ouvrirait le fichier
malveillant. Les réglages des alertes (affichage, historique, mode « Ne pas déranger ») sont ceux
de « Linux Defender » dans Configuration du système → Notifications. Les scans de clés USB utilisent les mêmes notifications ;
des menaces sur une clé donnent aussi une alerte critique.

### Activer ou désactiver la protection

Les paquets activent le service (démarrage à chaque démarrage de la machine) et le démarrent :

- à la première installation ;
- lors d'une mise à jour depuis une version antérieure à 1.0.2, où il était installé désactivé.

Ensuite, votre choix est respecté : un service désactivé le reste lors des mises à jour.

La case **Activer la protection en temps réel** de la page **Protection en temps réel** l'active
(démarrage immédiat et à chaque démarrage de la machine) ou la désactive, après le mot de passe
administrateur (voir [Diagnostic](#diagnostic) pour le programme d'aide). Activer celle de Linux
Defender désactive aussi le `clamav-clamonacc.service` de Debian et Ubuntu, qui ne doit pas tourner
en même temps. Sans programme d'aide (archive `.tar.gz`), la case est grisée. En ligne de commande,
pour la désactiver, puis la réactiver :

```sh
sudo systemctl disable --now linux-defender-onaccess.service
sudo systemctl enable --now linux-defender-onaccess.service
```

Sous Fedora, l'activation passe par une règle de préréglage installée par le paquet
(`/usr/lib/systemd/system-preset/80-linux-defender.preset`), qui l'emporte sur la règle par
défaut de Fedora (« désactiver les services inconnus »).

`clamonacc` n'est que recommandé par les paquets. S'il est absent, le service est simplement
ignoré (`ConditionPathExists`), sans échec répété, et la page indique comment l'installer ; une
fois `clamonacc` installé, démarrez le service avec la commande ci-dessus (ou redémarrez la
machine). `clamonacc` est lancé avec `--wait` : si clamd n'est pas encore prêt (signatures en
cours de téléchargement, socket pas encore configuré sous Fedora), il l'attend.

La page **Protection en temps réel** se met à jour toute seule. En cas d'échec, elle explique la
cause ; le détail est aussi dans `journalctl -u linux-defender-onaccess.service`.

Si `clamonacc` s'arrête sans que vous l'ayez demandé, systemd le relance 30 secondes plus tard
(`Restart=always`) et la page affiche « Protection en temps réel en erreur », avec la cause lue
dans le journal. `clamonacc` quitte en effet avec le code 0 même sur une erreur fatale (limite
inotify atteinte, plantage), exactement comme lors d'un arrêt demandé : seul systemd sait
distinguer les deux. Un arrêt demandé (`systemctl stop` ou `disable --now`) n'est jamais relancé.

Fichiers installés par les paquets (pas par l'archive `.tar.gz`) :

| Fichier | Rôle |
|---|---|
| `/usr/lib/systemd/system/linux-defender-onaccess.service` | service qui lance `clamonacc` (activé à l'installation) |
| `/usr/lib/systemd/system-preset/80-linux-defender.preset` | Fedora : active le service à l'installation |
| `/etc/linux-defender/clamonacc.conf` | configuration de `clamonacc` : socket de clamd, dossiers surveillés (`/home` par défaut), analyse à l'écriture |
| `/etc/logrotate.d/linux-defender` | rotation mensuelle du journal |
| `/usr/libexec/linux-defender-helper` | programme d'aide des corrections, lancé par `pkexec` (voir [Diagnostic](#diagnostic)) |
| `/usr/share/polkit-1/actions/io.github.memton80.linux-defender.policy` | action polkit du programme d'aide |

`clamd.conf` n'est pas modifié : `clamonacc` lit sa propre configuration. Après avoir modifié
`/etc/linux-defender/clamonacc.conf` (conservé lors des mises à jour), redémarrez le service :
`sudo systemctl restart linux-defender-onaccess.service`.

Le service tourne en root, mais limité aux deux capacités nécessaires. Si le socket de clamd
est réservé à un groupe (`LocalSocketMode 660` dans la configuration de clamd), `clamonacc` ne
peut pas s'y connecter : ajoutez ce groupe au service avec `sudo systemctl edit
linux-defender-onaccess.service`, en y écrivant `[Service]` puis
`SupplementaryGroups=<groupe du socket>`.

**Non testé à ce jour : Fedora avec SELinux actif.** Si le service échoue sous Fedora, les refus
de SELinux sont visibles avec `sudo ausearch -m avc -ts recent`.

### Paquet qui fournit clamonacc

| Distribution | Installation | Emplacement |
|---|---|---|
| Fedora | `sudo dnf install clamav clamd` | `/usr/bin/clamonacc` |
| Debian / Ubuntu | `sudo apt install clamav-daemon` | `/usr/sbin/clamonacc` |
| Arch Linux | `sudo pacman -S clamav` | |
| openSUSE | `sudo zypper install clamav` | |

Si `clamonacc` est absent, la page affiche la commande adaptée à la distribution détectée.

### Privilèges nécessaires

- **fanotify** exige les droits root et la capacité `CAP_SYS_ADMIN` : `clamonacc` tourne donc
  comme service système. Sans ces droits, il échoue avec `fanotify_init failed: Operation not
  permitted` (message expliqué dans la page).
- **`CAP_DAC_READ_SEARCH`** lui permet de suivre les dossiers privés (0700) des utilisateurs.
- Ces deux capacités suffisent : vérifié avec clamonacc 1.5.4 privé de toutes les autres.
- Comme pour les scans manuels, `clamonacc` transmet les fichiers ouverts à clamd (`--fdpass`) :
  clamd n'a pas besoin de pouvoir les lire lui-même.
- **L'application, elle, ne demande aucun privilège** pour tout cela : elle lit l'état du service
  auprès de systemd (lecture seule) et le journal de `clamonacc`. Seules l'activation et la
  désactivation (case de la page) passent par le programme d'aide et `pkexec`, avec le mot de
  passe administrateur.

### Journal

`clamonacc` écrit ses détections dans `/var/log/linux-defender/clamonacc.log`, que l'application
suit sans scrutation périodique (inotify). ClamAV crée ses journaux illisibles pour les
utilisateurs (droits 0640, root) : le service doit donc créer ce fichier à l'avance, lisible
(0644). Sinon, la page le signale. Ce journal ne contient que les détections et les erreurs, mais
il est lisible par tous les utilisateurs de la machine.

`clamonacc` n'horodate pas son journal : les détections antérieures au lancement de l'application
apparaissent avec la mention « Avant le lancement ».

Le journal est archivé chaque mois (6 archives compressées, `clamonacc.log.1.gz`...). `clamonacc`
ne rouvre jamais son journal (il ignore SIGHUP) : la rotation copie donc le fichier puis le vide
en place (`copytruncate`), et `clamonacc` continue d'y écrire. Les quelques lignes écrites
pendant la copie peuvent manquer à l'archive.

### Limitations connues

- **Détection seulement** : l'accès aux fichiers n'est pas bloqué (`OnAccessPrevention no`), et
  les fichiers infectés restent en place tant que vous ne les mettez pas en
  [quarantaine](#quarantaine).
- **Nombre de dossiers** : `clamonacc` pose une surveillance inotify sur chaque dossier sous les
  dossiers surveillés. Si leur nombre dépasse la limite du noyau au démarrage, `clamonacc` écrit
  `could not watch path '/home', No space left on device` et s'arrête ; la page le signale et
  systemd le relance toutes les 30 secondes. Pour comparer la limite au nombre de dossiers, puis
  l'augmenter :

  ```sh
  cat /proc/sys/fs/inotify/max_user_watches
  sudo find /home -type d | wc -l
  echo fs.inotify.max_user_watches=524288 | sudo tee /etc/sysctl.d/90-linux-defender.conf
  sudo sysctl --system
  ```

- **Erreurs « watch descriptor issue » dans le journal, sans gravité.** Des lignes comme :

  ```
  ERROR: ClamInotif: watch descriptor issue when adding watch for /home/alex/.var/app/app.zen_browser.zen/cache/zen/5alpemjo.Default (release)/safebrowsing-backup
  ERROR: ClamInotif: could not add element to hash table for .../safebrowsing-updating
  ERROR: ClamInotif: issue when adding watch for .../safebrowsing-backup/google4
  ```

  apparaissent quand un dossier est créé puis supprimé avant que `clamonacc` ait pu le surveiller.
  C'est le cas à chaque mise à jour des listes Safe Browsing de Firefox et de ses dérivés (Zen,
  y compris en Flatpak sous `~/.var/app/`) : `safebrowsing-updating` est renommé en
  `safebrowsing-backup`, puis supprimé aussitôt. Le dossier n'existant plus, il n'y a rien à
  protéger, et `clamonacc` continue normalement : l'application ignore ces lignes. Elles ne
  signalent pas la limite inotify (reproduites avec 143 dossiers pour une limite de 130 000) ;
  le vrai dépassement de limite se reconnaît à la ligne `could not watch path` ci-dessus.

  Aucun dossier n'est exclu par défaut. `OnAccessExcludePath` n'accepte pas de jokers
  (`~/.var/app/*/cache` est impossible), et les caches comme `~/.cache` sont justement des
  endroits où un programme malveillant peut déposer ses fichiers. Pour exclure malgré tout un
  dossier précis, ajoutez une ligne `OnAccessExcludePath /home/alex/.var/app/app.zen_browser.zen/cache`
  (chemin complet) à `/etc/linux-defender/clamonacc.conf`, puis redémarrez le service.

- **Montages réseau et FUSE** (NFS, SMB/CIFS, sshfs...) : les modifications faites depuis une
  autre machine sont invisibles pour fanotify, et les montages FUSE peuvent ne produire aucun
  événement. Ces fichiers ne sont pas protégés en temps réel ; un scan manuel reste possible.
- **Charge** : chaque fichier ouvert, créé, écrit ou renommé dans les dossiers surveillés est
  analysé par clamd. L'analyse à l'écriture (`OnAccessExtraScanning yes`) est nécessaire : sans
  elle, un fichier n'est analysé qu'à son ouverture, et un téléchargement (fichier ouvert vide
  puis rempli, ou écrit en `.part` puis renommé) n'est détecté que lorsque quelqu'un le rouvre.
  Les activités qui touchent beaucoup de fichiers (compilation, git, caches de navigateur,
  machines virtuelles) consomment donc du processeur. Les fichiers de plus de 5 Mo
  (`OnAccessMaxFileSize`) ne sont pas analysés.
- **Service de la distribution** : Debian et Ubuntu fournissent `clamav-clamonacc.service`, qui
  déplace les fichiers infectés dans `/root/quarantine`. Il ne doit pas tourner en même temps que
  celui de Linux Defender ; la page signale s'il est actif.
- Sous Fedora, clamd doit pouvoir lire les fichiers transmis : voir
  [la section SELinux](#fedora--autoriser-clamd-à-analyser-vos-fichiers-selinux).

## Compilation depuis les sources

### Dépendances

#### Fedora (44 et suivantes)

```sh
# Compilation
sudo dnf install cmake gcc-c++ ninja-build qt6-qtbase-devel

# À l'exécution : icônes SVG et UDisks2 (clés USB), déjà présents sur Fedora KDE
sudo dnf install qt6-qtsvg udisks2

# ClamAV
sudo dnf install clamav clamd clamav-update
```

#### Debian / Ubuntu (24.04 et suivantes)

```sh
# Compilation
sudo apt install build-essential cmake ninja-build qt6-base-dev

# À l'exécution : icônes SVG et UDisks2 (clés USB)
sudo apt install libqt6svg6 udisks2

# ClamAV
sudo apt install clamav-daemon clamav-freshclam
```

### Compilation

```sh
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure   # tests unitaires
```

Les tests n'ont besoin ni de clamd ni d'UDisks2 : ils utilisent un faux clamd et un faux
service UDisks2. Le test des clés USB démarre son propre bus D-Bus avec `dbus-run-session`
(paquet `dbus-daemon`, présent sur la plupart des systèmes) ; sans lui, ce test est ignoré.

Le binaire se trouve dans `build/bin/linux-defender`.

Option CMake disponible : `-DDEFENDER_BUILD_TESTS=OFF` pour ne pas compiler les tests.

Installation (binaire, fichier `.desktop` et icône) :

```sh
sudo cmake --install build --prefix /usr/local
```

## Paquets et CI

Le workflow [`.github/workflows/ci.yml`](.github/workflows/ci.yml) tourne à chaque push sur
`main`, à chaque pull request vers `main`, sur chaque tag `vX.Y.Z`, ou à la demande (onglet
Actions, « Run workflow »). Deux jobs en parallèle :

- **Ubuntu 24.04** : compilation, tests, paquet `.deb`, archive `.tar.gz` ;
- **Fedora 44** (conteneur) : compilation, tests, paquet `.rpm`.

Les tests passent avant toute création de paquet : si un test échoue, aucun paquet n'est
produit. Ils sont aussi relancés pendant la construction de chaque paquet.

### Publier une version

La version est écrite à un seul endroit : le fichier `VERSION` à la racine (`1.0.2`). CMake,
les paquets, la page de manuel et la fenêtre « À propos » la lisent tous là. Pour publier :

```sh
echo 1.0.3 > VERSION        # puis journal des modifications, commit, fusion dans main
git tag v1.0.3
git push origin v1.0.3
```

Le workflow crée alors la release GitHub avec les trois fichiers. Le tag doit correspondre au
fichier `VERSION` (sinon la CI s'arrête). Un tag avec suffixe (`v1.0.3-rc1`) crée une
préversion, notée `1.0.3~rc1` dans les paquets (le `~` la classe avant la version finale).

### Construire les paquets en local

Les scripts de `packaging/` sont ceux qu'utilise la CI (à lancer à la racine du dépôt) :

```sh
packaging/build-deb.sh              # Debian/Ubuntu : dpkg-dev, debhelper, + dépendances de compilation
packaging/build-rpm.sh              # Fedora : rpm-build, cmake-rpm-macros, systemd-rpm-macros, + dépendances de compilation
packaging/build-tarball.sh build    # à partir d'un dossier de build déjà compilé
```

Les fichiers produits sont dans `dist/`. Les recettes des paquets sont dans `packaging/debian/`
(`control`, `rules`, `postinst`...) et `packaging/rpm/linux-defender.spec`.

## Arborescence

```
linux-defender/
├── VERSION                   # numéro de version (seul endroit où le changer)
├── CMakeLists.txt            # projet, options, dépendances Qt
├── src/
│   ├── CMakeLists.txt        # cibles : defender_core, defender_system (bibliothèques) + linux-defender
│   ├── main.cpp              # point d'entrée : relie le cœur, le système et l'UI
│   ├── core/                 # logique métier : QtCore + QtNetwork, aucune dépendance à l'UI
│   │   ├── ClamdClient.*     # communication avec clamd (socket Unix, protocole natif)
│   │   ├── ClamdConfig.*     # configuration de clamd : socket, limites de taille
│   │   ├── ClamdWatcher.*    # vérification périodique de l'état de clamd
│   │   ├── ScanHistory.*     # historique des analyses (JSON)
│   │   ├── Quarantine.*      # quarantaine : fichiers retirés, rendus inertes, restaurables
│   │   ├── ScanJob.*         # un scan (FILDES), dans son propre thread, avec ses options
│   │   ├── ScanManager.*     # lance les scans, un à la fois, avec file d'attente
│   │   ├── Settings.*        # réglages (QSettings) et leurs valeurs par défaut
│   │   └── ThreatText.*      # nature d'une détection (menace, suspect, non analysé), textes des alertes
│   ├── system/               # intégration au système, sans UI
│   │   ├── UsbMonitor.*      # montage des clés USB (UDisks2 via D-Bus)
│   │   ├── SingleInstance.*  # une seule instance à la fois
│   │   ├── PrivilegedHelper.*   # corrections en root : programme d'aide lancé par pkexec
│   │   ├── ScanSchedule.*    # analyses planifiées (chaque jour, chaque semaine)
│   │   ├── SystemDiagnostics.*  # diagnostic de l'installation (évaluation pure + relevé du système)
│   │   ├── Autostart.*       # démarrage automatique (~/.config/autostart)
│   │   ├── DesktopNotifier.* # notifications du bureau (org.freedesktop.Notifications)
│   │   ├── OnAccessController.* # supervision de la protection en temps réel (clamonacc)
│   │   └── OnAccessLog.*     # suivi du journal de clamonacc (détections)
│   └── ui/                   # interface QtWidgets
│       ├── MainWindow.*      # fenêtre principale : barre latérale et pages
│       ├── DashboardPage.*   # page « Accueil » : bandeau d'état, tuiles, lancement des analyses
│       ├── ScanPanel.*       # page « Analyse » : progression, bilan, résultats filtrés
│       ├── OnAccessPanel.*   # page « Protection en temps réel » : état, détections
│       ├── QuarantinePanel.* # page « Quarantaine » : restauration, suppression définitive
│       ├── HistoryPanel.*    # page « Historique » : analyses passées et leurs menaces
│       ├── DiagnosticsPanel.*# page « Diagnostic » : vérifications, commandes, corrections
│       ├── HistoryModel.*    # liste des analyses de l'historique
│       ├── OnAccessModel.*   # liste des détections en temps réel
│       ├── ScanResultsModel.*# liste des fichiers analysés, et son filtre
│       ├── SettingsDialog.*  # dialogue « Paramètres », en pages
│       ├── TrayIcon.*        # icône, menu et notifications de la zone de notification
│       ├── Widgets.*         # cadres teintés, liste vide, liste de chemins modifiable
│       ├── FileActions.*     # afficher un fichier dans Dolphin, copier son chemin
│       └── StatusDisplay.*   # icônes, couleurs et textes partagés
├── data/
│   ├── icons/*.svg           # icônes SVG (placeholders à remplacer)
│   ├── linux-defender.desktop
│   ├── linux-defender.1.in   # page de manuel (man linux-defender), version ajoutée par CMake
│   ├── onaccess/             # service systemd, configuration de clamonacc, rotation du journal
│   └── helper/               # programme d'aide (script shell) et son action polkit
├── packaging/
│   ├── version.sh            # version : fichier VERSION (vérifiée contre le tag git)
│   ├── build-deb.sh, build-rpm.sh, build-tarball.sh
│   ├── debian/               # paquet .deb (debhelper)
│   └── rpm/                  # paquet .rpm (fichier spec)
├── tests/
│   ├── FakeClamd.h           # faux clamd (FILDES compris) : pas besoin de clamd pour les tests
│   └── tst_*.cpp             # un fichier de tests par classe
└── .github/workflows/ci.yml  # CI : tests, .deb, .rpm, .tar.gz, releases
```

## Feuille de route

- [x] Étape 1 : `ClamdClient`, connexion au socket et `PING`
- [x] Étape 2 : icône dans la zone de notification, fenêtre principale, statut de clamd
- [x] Étape 3 : scan à la demande (FILDES), scan automatique des clés USB (UDisks2), instance
      unique, démarrage automatique, paramètres
- [x] Étape 4 : CI GitHub Actions, paquets `.deb` et `.rpm`, archive `.tar.gz`, releases
- [x] Étape 5 : protection en temps réel (`clamonacc`) — supervision, détections, alertes et
      service systemd faits dans la **version 1.0.0**, service activé à l'installation depuis la
      **version 1.0.2** ; case d'activation dans la page, avec le diagnostic
- [x] Interface : tableau de bord, historique des analyses, analyse rapide et complète,
      exclusions, paramètres en pages
- [x] Diagnostic et corrections en un clic, menu de Dolphin, analyse rapide renforcée
      (emplacements sensibles, programmes en cours), analyses planifiées
- [x] Quarantaine : mise en quarantaine, restauration, suppression définitive
- [ ] Plus tard : scans en parallèle, son des alertes

Le détail des travaux en cours et à venir est dans [TODO.md](TODO.md).

## Historique des versions

Voir [CHANGELOG.md](CHANGELOG.md).

## Licence

Aucune licence n'a encore été choisie : en attendant, tous droits réservés.
