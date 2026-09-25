# Journal des modifications

Les versions publiées correspondent aux tags `vX.Y.Z` (voir les
[releases](https://github.com/memton80/linux-defender/releases)). Le numéro de version est écrit
dans le fichier `VERSION` ; avant la 1.0.2, les paquets construits hors tag portaient la version
`0.0.0~dev`.

## [Non publié]

### Corrigé

- **Fichiers trop gros pour clamd affichés « sains ».** Au-delà de sa limite `MaxFileSize`
  (100 Mo par défaut, 25 Mo dans la configuration installée par Debian et Ubuntu) ou de 2 Go,
  clamd répond « OK » sans lire le fichier (vérifié avec ClamAV 1.5.4, virus compris). L'application lit maintenant cette limite dans la configuration de
  clamd et signale ces fichiers **« non analysé »**. La page « Analyse » des paramètres affiche
  la limite trouvée.
- **Soupçons présentés comme des menaces.** Les détections heuristiques (`Heuristics.*` :
  hameçonnage, exécutable malformé, macros...) et les programmes potentiellement indésirables
  (`PUA.*`) sont classés **« suspect »** : notification sans alerte critique, bandeau orange au
  lieu de rouge. Les archives chiffrées et les limites dépassées (`Heuristics.Encrypted.*`,
  `Heuristics.Limits.Exceeded.*`) sont classées **« non analysé »** ; détectées en temps réel,
  elles ne déclenchent plus d'alerte.

### Ajouté

- **Page Diagnostic** : clamd (service arrêté, en échec ou absent ; ligne `Example` et socket
  désactivé de Fedora), accès au socket (groupe à rejoindre, ou session à rouvrir), signatures et
  `clamav-freshclam`, SELinux (`antivirus_can_scan_system`), `AlertExceedsMax`, protection en temps
  réel et limite inotify. Chaque problème a son explication, la commande exacte à copier et, si
  possible, une **correction en un clic**. Le bandeau de l'accueil y mène (« Résoudre »).
- **Case « Activer la protection en temps réel »** dans la page du même nom.
- Programme d'aide `/usr/libexec/linux-defender-helper` (paquets `.deb` et `.rpm`), lancé par
  `pkexec` sous l'action polkit `io.github.memton80.linux-defender.manage` : mot de passe
  administrateur retenu quelques minutes, liste fermée d'actions, copie de la configuration de
  clamd avant modification. Les paquets recommandent `pkexec` (Debian, Ubuntu) ou `polkit` (Fedora).
- Filtre et compteur **Avertissements** (fichiers suspects et non analysés) dans la page
  « Analyse », colonne dans l'historique, et liste des avertissements dans le détail d'une
  analyse. Les historiques des versions précédentes restent lisibles.
- Infobulle des résultats : signification de la signature en clair.

## [1.0.2] — 2026-09-24

### Modifié

- **Protection en temps réel activée à l'installation.** Les paquets `.deb` et `.rpm` activent et
  démarrent `linux-defender-onaccess.service` à la première installation, et lors d'une mise à
  jour depuis une version antérieure (où il était installé désactivé). Ensuite, un service
  désactivé par l'administrateur le reste. Sous Fedora, l'activation passe par une règle de
  préréglage (`80-linux-defender.preset`). Sans `clamonacc` (seulement recommandé), le service est
  ignoré au lieu d'échouer toutes les 30 secondes ; la page indique comment l'installer, et
  comment réactiver un service désactivé.
- **Version 1.0.2 partout** : un seul fichier `VERSION` fournit la version à l'application
  (« À propos », `--version`, barre latérale), aux paquets, à la page de manuel et à l'archive.
  Les builds hors tag n'affichent plus `0.0.0-dev`. Un tag de release qui ne correspond pas au
  fichier `VERSION` arrête la CI.

### Ajouté

- **Nouvelle interface.** Barre latérale avec quatre pages :
  - **Accueil** : bandeau qui résume l'état de la protection (vert, orange ou rouge) avec l'action
    la plus utile, tuiles d'état (clamd, signatures et leur âge, temps réel, dernière analyse, clés
    USB, historique) et boutons d'analyse ;
  - **Analyse** : progression détaillée (fichier en cours, compteurs, durée), bilan, filtres
    (menaces, erreurs, sains), recherche, export CSV ;
  - **Protection en temps réel** : état, dossiers surveillés, détections ;
  - **Historique** : analyses passées, avec leur bilan et leurs menaces.
  Clic droit sur un fichier : l'afficher dans le gestionnaire de fichiers, copier son chemin ou le
  nom de la menace. Couleurs du thème, sans feuille de style (Breeze clair et sombre).
- **Analyse rapide** (Téléchargements, Bureau, Documents) et **analyse complète** (dossier
  personnel), aussi depuis le menu de l'icône.
- **Historique des analyses**, enregistré dans `~/.local/share/linux-defender/history.json`.
- **Paramètres en pages** (Général, Analyse, Clés USB, Notifications, clamd), avec « Appliquer » et
  « Valeurs par défaut ». Nouveaux réglages : dossiers de l'analyse rapide, exclusions, fichiers
  cachés, taille maximale des fichiers, notifications (fin d'analyse, clés USB, temps réel, clamd
  perdu, signatures obsolètes), fermeture de la fenêtre, taille de l'historique, intervalle de
  vérification de clamd, âge maximal des signatures, test de connexion au socket.
- Textes de Qt (boutons standard, sélecteur de fichiers) traduits hors Plasma aussi.

## [1.0.1] — 2026-09-24

### Corrigé

- **Protection en temps réel : plus aucune détection affichée après la rotation mensuelle du
  journal.** La rotation renommait le journal, créait un fichier vide, puis envoyait SIGHUP à
  `clamonacc` pour qu'il le rouvre. Or `clamonacc` ignore SIGHUP et ne rouvre jamais son
  journal : il continuait d'écrire dans l'ancien fichier, aussitôt compressé puis supprimé. Les
  détections étaient donc perdues, jusqu'au redémarrage du service. La rotation copie maintenant
  le journal puis le vide en place (`copytruncate`), et `clamonacc` continue d'y écrire.
- Le service n'a plus d'`ExecReload`, qui envoyait ce SIGHUP sans effet.

La mise à jour du paquet redémarre le service s'il tournait : sur un système déjà touché, les
nouvelles détections s'affichent de nouveau sans autre manipulation (celles écrites dans le
fichier supprimé sont perdues). Si vous avez modifié `/etc/logrotate.d/linux-defender`, la
nouvelle version est installée à côté (`.rpmnew` sous Fedora, question de `dpkg` sous Debian et
Ubuntu) : reprenez-y `copytruncate`.

## [1.0.0] — 2026-09-24

Première version publiée.

### Fonctionnalités

- **Scan à la demande** de fichiers et de dossiers : l'application ouvre chaque fichier avec vos
  droits et le transmet à clamd (FILDES), qui peut ainsi analyser le dossier personnel et les
  clés USB sans pouvoir les lire lui-même.
- **Scan automatique des clés USB** au montage (UDisks2), avec notifications.
- **Protection en temps réel** (supervision de `clamonacc`) :
  - service `linux-defender-onaccess.service` installé par les paquets, **désactivé** par défaut ;
  - analyse à l'ouverture et à l'écriture des fichiers (`OnAccessExtraScanning yes`) : un
    téléchargement est détecté dès qu'il est terminé, y compris depuis une application Flatpak ;
  - relance automatique si `clamonacc` s'arrête sans qu'on l'ait demandé (`Restart=always`) ;
  - onglet **Protection en temps réel** : état du service, cause des échecs (limite inotify,
    clamd injoignable, privilèges...) lue dans le journal de `clamonacc`, liste des détections.
- **Alertes du bureau** (`org.freedesktop.Notifications`) : nom du fichier, nature de la menace
  en clair (« Cheval de Troie (Windows) », « Fichier de test EICAR (inoffensif) »), dossier
  raccourci ; alerte critique qui reste affichée, boutons « Afficher les détails » et « Ouvrir le
  dossier », détections rapprochées regroupées, icônes Breeze sous Plasma.
- Icône dans la zone de notification avec l'état de clamd, instance unique, démarrage
  automatique à l'ouverture de session, paramètres.
- Paquets `.rpm` (Fedora 44) et `.deb` (Ubuntu 24.04 et suivantes), archive `.tar.gz`,
  intégration continue GitHub Actions.

### Limites connues

- La protection en temps réel s'active avec `systemctl` : la case des Paramètres arrivera dans
  une prochaine version.
- Détection seulement : les fichiers infectés ne sont ni bloqués, ni supprimés, ni mis en
  quarantaine.
- Après la rotation mensuelle du journal de `clamonacc`, l'application ne voit plus les nouvelles
  détections tant que le service n'a pas redémarré (redémarrage de la machine, ou
  `sudo systemctl restart linux-defender-onaccess.service`). Correction prévue.

### Corrigé depuis les versions de développement (`0.0.0~dev`)

- Paquets `.deb` et `.rpm` : le service de protection en temps réel, sa configuration et la
  rotation de son journal manquaient ; `daemon-reload` à l'installation, arrêt à la
  désinstallation, redémarrage lors d'une mise à jour s'il tournait déjà.
- Les téléchargements (fichier ouvert vide puis rempli, ou `.part` renommé) n'étaient détectés
  qu'à la réouverture du fichier.
- Un arrêt inattendu de `clamonacc` (qui quitte avec le code 0 même sur une erreur fatale)
  n'était pas relancé, et l'onglet affichait « désactivée ».
- Diagnostic des erreurs vérifié sur les messages réels de `clamonacc` 1.5.4 : limite inotify
  reconnue, `Wait timeout exceeded` (écrit sans `ERROR:`) pris en compte, erreurs sans gravité
  (dossiers de cache supprimés avant d'être surveillés) ignorées, erreur d'un lancement
  précédent oubliée.
- L'application voit le service apparaître ou changer d'état sans redémarrer.
