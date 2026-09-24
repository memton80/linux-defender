# linux-defender

Interface graphique native et légère pour l'antivirus [ClamAV](https://www.clamav.net/),
pensée pour KDE Plasma (thème Breeze clair/sombre automatique).
Écrite en C++ / Qt 6 (QtWidgets). Elle communique directement avec le démon `clamd`
par son socket Unix, avec le protocole natif de clamd.

> **État : étape 3.** Scan à la demande de fichiers et de dossiers, scan automatique des clés
> USB, icône dans la zone de notification avec notifications, instance unique, démarrage
> automatique et paramètres. Prochaine étape : la CI et les paquets `.deb` / `.rpm`.

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
│   │   └── Autostart.*       # démarrage automatique (~/.config/autostart)
│   └── ui/                   # interface QtWidgets
│       ├── MainWindow.*      # fenêtre principale
│       ├── ScanPanel.*       # section « Scan » : boutons, progression, résultats
│       ├── ScanResultsModel.*# liste des fichiers analysés
│       ├── SettingsDialog.*  # dialogue « Paramètres »
│       ├── TrayIcon.*        # icône, menu et notifications de la zone de notification
│       └── StatusDisplay.*   # icônes et textes partagés
├── data/
│   ├── icons/*.svg           # icônes SVG (placeholders à remplacer)
│   └── linux-defender.desktop
├── tests/
│   ├── FakeClamd.h           # faux clamd (FILDES compris) : pas besoin de clamd pour les tests
│   └── tst_*.cpp             # un fichier de tests par classe
└── .github/workflows/        # (à venir) CI : binaire, .deb, .rpm
```

## Dépendances

### Fedora (44 et suivantes)

```sh
# Compilation
sudo dnf install cmake gcc-c++ ninja-build qt6-qtbase-devel

# À l'exécution : icônes SVG et UDisks2 (clés USB), déjà présents sur Fedora KDE
sudo dnf install qt6-qtsvg udisks2

# ClamAV
sudo dnf install clamav clamd clamav-update
```

### Debian / Ubuntu (24.04 et suivantes)

```sh
# Compilation
sudo apt install build-essential cmake ninja-build qt6-base-dev

# À l'exécution : icônes SVG et UDisks2 (clés USB)
sudo apt install libqt6svg6 udisks2

# ClamAV
sudo apt install clamav-daemon clamav-freshclam
```

## Compilation

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

```sh
./build/bin/linux-defender                           # ouvre la fenêtre, socket détecté automatiquement
./build/bin/linux-defender --background              # démarre directement dans la zone de notification
./build/bin/linux-defender --socket /chemin/clamd.sock
```

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

## Feuille de route

- [x] Étape 1 : `ClamdClient`, connexion au socket et `PING`
- [x] Étape 2 : icône dans la zone de notification, fenêtre principale, statut de clamd
- [x] Étape 3 : scan à la demande (FILDES), scan automatique des clés USB (UDisks2), instance
      unique, démarrage automatique, paramètres
- [ ] Étape 4 : CI GitHub Actions (binaire, `.deb`, `.rpm`)
- [ ] Plus tard : protection en temps réel (`clamonacc`), quarantaine, historique, planification,
      scans en parallèle, KNotifications
