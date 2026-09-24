# Journal des modifications

Les versions publiées correspondent aux tags `vX.Y.Z` (voir les
[releases](https://github.com/memton80/linux-defender/releases)). Les paquets construits hors tag
portent la version `0.0.0~dev`.

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
