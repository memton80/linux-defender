# linux-defender

Interface graphique native et légère pour l'antivirus [ClamAV](https://www.clamav.net/),
pensée pour KDE Plasma (thème Breeze clair/sombre automatique).
Écrite en C++ / Qt 6 (QtWidgets). Elle communique directement avec le démon `clamd`
par son socket Unix, avec le protocole natif de clamd.

> **État : étape 2.** L'application tourne en arrière-plan avec une icône dans la zone de
> notification. Elle affiche l'état de clamd (connecté, ou inaccessible avec la raison).
> Le scan arrive à l'étape 3.

## Arborescence

```
linux-defender/
├── CMakeLists.txt            # projet, options, dépendances Qt
├── src/
│   ├── CMakeLists.txt        # cibles : defender_core (bibliothèque) + linux-defender (exécutable)
│   ├── main.cpp              # point d'entrée : relie le cœur et l'UI
│   ├── core/                 # logique métier : QtCore + QtNetwork, aucune dépendance à l'UI
│   │   ├── ClamdClient.*     # communication avec clamd (socket Unix, protocole natif)
│   │   └── ClamdWatcher.*    # vérification périodique de l'état de clamd
│   ├── system/               # (à venir) intégration système : clés USB via UDisks2/D-Bus
│   └── ui/                   # interface QtWidgets
│       ├── MainWindow.*      # fenêtre principale
│       ├── TrayIcon.*        # icône et menu de la zone de notification
│       └── StatusDisplay.*   # icône et textes de l'état de clamd (partagés)
├── data/
│   ├── icons/*.svg           # icônes SVG (placeholders à remplacer)
│   └── linux-defender.desktop
├── tests/
│   ├── FakeClamd.h           # faux clamd : pas besoin de clamd pour lancer les tests
│   ├── tst_clamdclient.cpp
│   └── tst_clamdwatcher.cpp
└── .github/workflows/        # (à venir) CI : binaire, .deb, .rpm
```

## Dépendances

### Fedora (44 et suivantes)

```sh
# Compilation
sudo dnf install cmake gcc-c++ ninja-build qt6-qtbase-devel

# Affichage des icônes SVG (déjà présent sur Fedora KDE)
sudo dnf install qt6-qtsvg

# ClamAV
sudo dnf install clamav clamd clamav-update
```

### Debian / Ubuntu (24.04 et suivantes)

```sh
# Compilation
sudo apt install build-essential cmake ninja-build qt6-base-dev

# Affichage des icônes SVG
sudo apt install libqt6svg6

# ClamAV
sudo apt install clamav-daemon clamav-freshclam
```

## Compilation

```sh
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure   # tests unitaires
```

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

## Utilisation

```sh
./build/bin/linux-defender                           # ouvre la fenêtre, socket détecté automatiquement
./build/bin/linux-defender --background              # démarre directement dans la zone de notification
./build/bin/linux-defender --socket /chemin/clamd.sock
```

Fermer la fenêtre ne quitte pas l'application : elle reste active dans la zone de notification.
- Clic gauche sur l'icône : afficher ou masquer la fenêtre.
- Clic droit : état de clamd, « Vérifier l'état de clamd », « Ouvrir la fenêtre », « Quitter ».

L'état de clamd est vérifié toutes les 30 secondes, à l'ouverture de la fenêtre et sur demande.
La vérification utilise la commande `VERSION` de clamd, qui donne aussi la version du moteur
et la date des signatures.

Le socket est détecté ainsi :
1. la directive `LocalSocket` de `/etc/clamd.d/scan.conf`, `/etc/clamav/clamd.conf` ou `/etc/clamd.conf` ;
2. sinon, les emplacements usuels `/run/clamd.scan/clamd.sock` (Fedora) et `/run/clamav/clamd.ctl` (Debian/Ubuntu).

## Feuille de route

- [x] Étape 1 : `ClamdClient`, connexion au socket et `PING`
- [x] Étape 2 : icône dans la zone de notification, fenêtre principale, statut de clamd
- [ ] Étape 3 : scan à la demande d'un fichier ou d'un dossier
- [ ] Étape 4 : scan automatique des clés USB branchées (UDisks2)
- [ ] Étape 5 : CI GitHub Actions (binaire, `.deb`, `.rpm`)
- [ ] Plus tard : protection en temps réel (`clamonacc`), quarantaine, historique, planification, KNotifications
