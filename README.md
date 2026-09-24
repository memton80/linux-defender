# linux-defender

[![CI](https://github.com/memton80/linux-defender/actions/workflows/ci.yml/badge.svg)](https://github.com/memton80/linux-defender/actions/workflows/ci.yml)

Interface graphique native et légère pour l'antivirus [ClamAV](https://www.clamav.net/),
pensée pour KDE Plasma (thème Breeze clair/sombre automatique).
Écrite en C++ / Qt 6 (QtWidgets). Elle communique directement avec le démon `clamd`
par son socket Unix, avec le protocole natif de clamd.

Fonctions : scan à la demande de fichiers et de dossiers, scan automatique des clés USB au
montage, icône dans la zone de notification avec l'état de clamd et des notifications,
démarrage automatique à l'ouverture de session.

## Installation

Les paquets de chaque version sont sur la page des releases :
**<https://github.com/memton80/linux-defender/releases>**.

Chaque release contient trois fichiers (remplacez `X.Y.Z` par la version) :

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
de base et plugin SVG) déjà installé sur le système.

```sh
tar -xzf linux-defender-X.Y.Z-linux-x86_64.tar.gz
./linux-defender-X.Y.Z-linux-x86_64/bin/linux-defender

# Ou installation pour votre utilisateur seulement (menu des applications compris) :
cp -r linux-defender-X.Y.Z-linux-x86_64/bin linux-defender-X.Y.Z-linux-x86_64/share ~/.local/
```

### Versions de développement

Chaque push sur `main` produit aussi les trois fichiers, en version `0.0.0-dev` : onglet
[Actions](https://github.com/memton80/linux-defender/actions/workflows/ci.yml), choisir un run, section « Artifacts »
(connexion à GitHub nécessaire).

## Configurer clamd

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
```

Depuis une compilation des sources, le binaire est `build/bin/linux-defender`. Aide complète :
`man linux-defender`.

Fermer la fenêtre ne quitte pas l'application : elle reste active dans la zone de notification.
- Clic gauche sur l'icône : afficher ou masquer la fenêtre.
- Clic droit : état de clamd, « Vérifier l'état de clamd », « Scanner un dossier… »,
  « Arrêter le scan » (pendant un scan), « Ouvrir la fenêtre », « Quitter ».

L'application ne se lance qu'une fois : la relancer réaffiche simplement la fenêtre existante.

### Scan

Boutons « Scanner des fichiers… » et « Scanner un dossier… » de la fenêtre. Le dossier est
parcouru récursivement ; les liens symboliques ne sont pas suivis, et `/proc`, `/sys` et `/dev`
sont ignorés. Chaque fichier est ouvert par l'application avec vos droits, puis transmis à clamd
(comme `clamdscan --fdpass`) : clamd peut donc analyser votre dossier personnel et vos clés USB
sans avoir le droit de les lire lui-même.

La liste affiche les menaces en premier (avec le nom donné par clamd), puis les erreurs, puis
les fichiers sains. Pour limiter la mémoire utilisée, seuls les 10 000 premiers fichiers sains
sont listés ; le résumé compte tous les fichiers.

### Clés USB

Quand une clé USB ou une carte mémoire est **montée**, elle est scannée automatiquement, avec une
notification au début et à la fin du scan. Sous Plasma, une clé est montée quand vous l'ouvrez
(Dolphin, notification « Périphériques ») ou dès le branchement si le montage automatique est
activé (Configuration du système → Disques et caméras → Montage automatique des périphériques).

Tant qu'un scan lit la clé, elle ne peut pas être éjectée : arrêtez le scan depuis le menu de
l'icône (« Arrêter le scan ») si besoin.

### Paramètres

Bouton « Paramètres… » de la fenêtre :
- **Socket de clamd** : à renseigner seulement si la détection automatique échoue ;
- **Scanner automatiquement les clés USB** : activé par défaut ;
- **Lancer au démarrage de la session** : crée `~/.config/autostart/linux-defender.desktop`, qui
  lance l'application avec `--background`.

Les réglages sont enregistrés dans `~/.config/linux-defender/linux-defender.conf`.

L'état de clamd est vérifié toutes les 30 secondes, à l'ouverture de la fenêtre et sur demande.
La vérification utilise la commande `VERSION` de clamd, qui donne aussi la version du moteur
et la date des signatures.

Le socket utilisé est, dans l'ordre :
1. celui de l'option `--socket` ;
2. celui des paramètres ;
3. la directive `LocalSocket` de `/etc/clamd.d/scan.conf`, `/etc/clamav/clamd.conf` ou `/etc/clamd.conf` ;
4. sinon, les emplacements usuels `/run/clamd.scan/clamd.sock` (Fedora) et `/run/clamav/clamd.ctl` (Debian/Ubuntu).

## Protection en temps réel (clamonacc)

> **En cours (étape 5).** L'application affiche déjà l'état de la protection en temps réel et les
> menaces détectées par `clamonacc`. Le service systemd qui lance `clamonacc`, et son activation
> depuis « Paramètres », arrivent dans un second temps.

`clamonacc` est le programme de ClamAV qui surveille les fichiers en temps réel : le noyau
(fanotify) lui signale chaque fichier ouvert ou modifié dans les dossiers surveillés, et il le
transmet à clamd pour analyse. Linux Defender ne refait pas ce travail : il **supervise** le
service `linux-defender-onaccess.service` qui lance `clamonacc`, et lit son journal pour vous
prévenir de chaque détection :

- notification système immédiate, avec le nom du fichier et celui de la menace ;
- icône de la zone de notification en alerte ;
- onglet **Protection en temps réel** de la fenêtre : état du service et liste des détections
  (distincte des résultats des scans manuels).

Les fichiers détectés ne sont **ni supprimés ni déplacés** : l'application indique seulement leur
emplacement.

### Paquet qui fournit clamonacc

| Distribution | Installation | Emplacement |
|---|---|---|
| Fedora | `sudo dnf install clamav clamd` | `/usr/bin/clamonacc` |
| Debian / Ubuntu | `sudo apt install clamav-daemon` | `/usr/sbin/clamonacc` |
| Arch Linux | `sudo pacman -S clamav` | |
| openSUSE | `sudo zypper install clamav` | |

Si `clamonacc` est absent, l'onglet affiche la commande adaptée à la distribution détectée.

### Privilèges nécessaires

- **fanotify** exige les droits root et la capacité `CAP_SYS_ADMIN` : `clamonacc` tourne donc
  comme service système. Sans ces droits, il échoue avec `fanotify_init failed: Operation not
  permitted` (message expliqué dans l'onglet).
- **`CAP_DAC_READ_SEARCH`** lui permet de suivre les dossiers privés (0700) des utilisateurs.
- Ces deux capacités suffisent : vérifié avec clamonacc 1.5.4 privé de toutes les autres.
- Comme pour les scans manuels, `clamonacc` transmet les fichiers ouverts à clamd (`--fdpass`) :
  clamd n'a pas besoin de pouvoir les lire lui-même.
- **L'application, elle, ne demande aucun privilège** pour tout cela : elle lit l'état du service
  auprès de systemd (lecture seule) et le journal de `clamonacc`.

### Journal

`clamonacc` écrit ses détections dans `/var/log/linux-defender/clamonacc.log`, que l'application
suit sans scrutation périodique (inotify). ClamAV crée ses journaux illisibles pour les
utilisateurs (droits 0640, root) : le service doit donc créer ce fichier à l'avance, lisible
(0644). Sinon, l'onglet le signale. Ce journal ne contient que les détections et les erreurs, mais
il est lisible par tous les utilisateurs de la machine.

`clamonacc` n'horodate pas son journal : les détections antérieures au lancement de l'application
apparaissent avec la mention « Avant le lancement ».

### Limitations connues

- **Détection seulement** : l'accès aux fichiers n'est pas bloqué (`OnAccessPrevention no`), et
  les fichiers infectés restent en place.
- **Nombre de dossiers** : `clamonacc` suit les sous-dossiers des dossiers surveillés avec
  inotify. Un très grand nombre de dossiers peut dépasser la limite du noyau ; l'onglet le
  signale. Pour l'augmenter :

  ```sh
  echo fs.inotify.max_user_watches=524288 | sudo tee /etc/sysctl.d/90-linux-defender.conf
  sudo sysctl --system
  ```

- **Montages réseau et FUSE** (NFS, SMB/CIFS, sshfs...) : les modifications faites depuis une
  autre machine sont invisibles pour fanotify, et les montages FUSE peuvent ne produire aucun
  événement. Ces fichiers ne sont pas protégés en temps réel ; un scan manuel reste possible.
- **Charge** : chaque fichier ouvert ou modifié dans les dossiers surveillés est analysé par
  clamd. Les activités qui touchent beaucoup de fichiers (compilation, git, caches de
  navigateur, machines virtuelles) consomment donc du processeur. Les fichiers de plus de 5 Mo
  (`OnAccessMaxFileSize`) ne sont pas analysés.
- **Service de la distribution** : Debian et Ubuntu fournissent `clamav-clamonacc.service`, qui
  déplace les fichiers infectés dans `/root/quarantine`. Il ne doit pas tourner en même temps que
  celui de Linux Defender ; l'onglet signale s'il est actif.
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

```sh
git tag v0.1.0
git push origin v0.1.0
```

Le workflow crée alors la release GitHub avec les trois fichiers. Un tag avec suffixe
(`v0.1.0-rc1`) crée une préversion.

La version vient du tag : `v1.2.3` donne `1.2.3`. Hors tag, c'est `0.0.0-dev`, notée
`0.0.0~dev` dans les paquets (le `~` classe cette version avant toute version publiée).

### Construire les paquets en local

Les scripts de `packaging/` sont ceux qu'utilise la CI (à lancer à la racine du dépôt) :

```sh
packaging/build-deb.sh              # Debian/Ubuntu : dpkg-dev, debhelper, + dépendances de compilation
packaging/build-rpm.sh              # Fedora : rpm-build, cmake-rpm-macros, + dépendances de compilation
packaging/build-tarball.sh build    # à partir d'un dossier de build déjà compilé
```

Les fichiers produits sont dans `dist/`. Les recettes des paquets sont dans `packaging/debian/`
(`control`, `rules`, `postinst`...) et `packaging/rpm/linux-defender.spec`.

## Arborescence

```
linux-defender/
├── CMakeLists.txt            # projet, options, dépendances Qt
├── src/
│   ├── CMakeLists.txt        # cibles : defender_core, defender_system (bibliothèques) + linux-defender
│   ├── main.cpp              # point d'entrée : relie le cœur, le système et l'UI
│   ├── core/                 # logique métier : QtCore + QtNetwork, aucune dépendance à l'UI
│   │   ├── ClamdClient.*     # communication avec clamd (socket Unix, protocole natif)
│   │   ├── ClamdWatcher.*    # vérification périodique de l'état de clamd
│   │   ├── ScanJob.*         # un scan (FILDES), dans son propre thread
│   │   ├── ScanManager.*     # lance les scans, un à la fois, avec file d'attente
│   │   └── Settings.*        # réglages (QSettings)
│   ├── system/               # intégration au système, sans UI
│   │   ├── UsbMonitor.*      # montage des clés USB (UDisks2 via D-Bus)
│   │   ├── SingleInstance.*  # une seule instance à la fois
│   │   ├── Autostart.*       # démarrage automatique (~/.config/autostart)
│   │   ├── OnAccessController.* # supervision de la protection en temps réel (clamonacc)
│   │   └── OnAccessLog.*     # suivi du journal de clamonacc (détections)
│   └── ui/                   # interface QtWidgets
│       ├── MainWindow.*      # fenêtre principale
│       ├── ScanPanel.*       # onglet « Scan » : boutons, progression, résultats
│       ├── OnAccessPanel.*   # onglet « Protection en temps réel » : état, détections
│       ├── OnAccessModel.*   # liste des détections en temps réel
│       ├── ScanResultsModel.*# liste des fichiers analysés
│       ├── SettingsDialog.*  # dialogue « Paramètres »
│       ├── TrayIcon.*        # icône, menu et notifications de la zone de notification
│       └── StatusDisplay.*   # icônes et textes partagés
├── data/
│   ├── icons/*.svg           # icônes SVG (placeholders à remplacer)
│   ├── linux-defender.desktop
│   └── linux-defender.1      # page de manuel (man linux-defender)
├── packaging/
│   ├── version.sh            # version à partir du tag git
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
- [ ] Étape 5 : protection en temps réel (`clamonacc`) — supervision et détections faites ;
      service systemd et activation depuis « Paramètres » à venir
- [ ] Plus tard : quarantaine, historique, planification, scans en parallèle, KNotifications

## Licence

Aucune licence n'a encore été choisie : en attendant, tous droits réservés.
