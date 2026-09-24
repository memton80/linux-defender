# linux-dedender

Interface graphique native et légère pour l'antivirus [ClamAV](https://www.clamav.net/),
pensée pour KDE Plasma (thème Breeze clair/sombre automatique).
Écrite en C++ / Qt 6 (QtWidgets). Elle communique directement avec le démon `clamd`
par son socket Unix, avec le protocole natif de clamd.

> **État : étape 1.** Seule la communication avec clamd existe pour l'instant.
> Le binaire envoie `PING` à clamd et affiche le résultat. L'interface graphique arrive à l'étape 2.

## Arborescence

```
linux-dedender/
├── CMakeLists.txt            # projet, options, dépendances Qt
├── src/
│   ├── CMakeLists.txt        # cibles : dedender_core (bibliothèque) + linux-dedender (exécutable)
│   ├── main.cpp              # point d'entrée
│   ├── core/                 # logique métier : QtCore + QtNetwork, aucune dépendance à l'UI
│   │   └── ClamdClient.*     # communication avec clamd (socket Unix, protocole natif)
│   ├── system/               # (à venir) intégration système : clés USB via UDisks2/D-Bus
│   └── ui/                   # (à venir) fenêtre principale, icône de notification
├── data/                     # (à venir) icônes SVG, fichier .desktop
├── tests/
│   └── tst_clamdclient.cpp   # tests avec un faux clamd : pas besoin de clamd pour les lancer
└── .github/workflows/        # (à venir) CI : binaire, .deb, .rpm
```

## Dépendances

### Fedora (44 et suivantes)

```sh
# Compilation
sudo dnf install cmake gcc-c++ ninja-build qt6-qtbase-devel

# ClamAV
sudo dnf install clamav clamd clamav-update
```

### Debian / Ubuntu (24.04 et suivantes)

```sh
# Compilation
sudo apt install build-essential cmake ninja-build qt6-base-dev

# ClamAV
sudo apt install clamav-daemon clamav-freshclam
```

## Compilation

```sh
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure   # tests unitaires
```

Le binaire se trouve dans `build/bin/linux-dedender`.

Option CMake disponible : `-DDEDENDER_BUILD_TESTS=OFF` pour ne pas compiler les tests.

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

## Utilisation (étape 1)

```sh
./build/bin/linux-dedender                           # socket détecté automatiquement
./build/bin/linux-dedender --socket /chemin/clamd.sock
```

Le socket est détecté ainsi :
1. la directive `LocalSocket` de `/etc/clamd.d/scan.conf`, `/etc/clamav/clamd.conf` ou `/etc/clamd.conf` ;
2. sinon, les emplacements usuels `/run/clamd.scan/clamd.sock` (Fedora) et `/run/clamav/clamd.ctl` (Debian/Ubuntu).

## Feuille de route

- [x] Étape 1 : `ClamdClient`, connexion au socket et `PING`
- [ ] Étape 2 : icône dans la zone de notification, fenêtre principale, statut de clamd
- [ ] Étape 3 : scan à la demande d'un fichier ou d'un dossier
- [ ] Étape 4 : scan automatique des clés USB branchées (UDisks2)
- [ ] Étape 5 : CI GitHub Actions (binaire, `.deb`, `.rpm`)
- [ ] Plus tard : protection en temps réel (`clamonacc`), quarantaine, historique, planification, KNotifications
