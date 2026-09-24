# Journal des modifications

Les versions publiées correspondent aux tags `vX.Y.Z` (voir les
[releases](https://github.com/memton80/linux-defender/releases)). Les paquets construits hors tag
portent la version `0.0.0~dev`.

## [Non publié]

### Corrigé

- **Protection en temps réel : les téléchargements n'étaient pas détectés.** `clamonacc`
  n'analysait un fichier qu'à son ouverture, donc vide pour un fichier ouvert puis rempli
  (téléchargement, `.part` renommé) : la menace n'était vue qu'à la réouverture du fichier. La
  configuration active maintenant l'analyse à l'écriture (`OnAccessExtraScanning yes`).
- **Arrêt inattendu de `clamonacc` non relancé et affiché comme « désactivée ».** `clamonacc`
  quitte avec le code 0 même sur une erreur fatale (limite inotify atteinte, plantage) :
  `Restart=on-failure` ne le relançait pas. Le service utilise maintenant `Restart=always`, et
  l'onglet affiche « en erreur » avec la cause tant que systemd le relance.
- Diagnostic des erreurs de `clamonacc`, vérifié sur les messages réels de la version 1.5.4 :
  - la limite inotify atteinte (`ClamInotif: could not watch path ..., No space left on device`)
    n'était jamais reconnue ;
  - `Wait timeout exceeded; Could not connect to clamd` (écrit sans `ERROR:`) était ignoré ;
  - les erreurs sans gravité (dossier supprimé avant d'être surveillé, par exemple par Zen ou
    Firefox dans leur cache) remplaçaient la vraie cause d'un échec ; elles sont ignorées ;
  - la cause d'un échec survenu avant le lancement de l'application est lue dans le journal, et
    une erreur d'un lancement précédent de `clamonacc` n'est plus utilisée pour expliquer un
    nouvel échec.

- **Paquets `.deb` et `.rpm` : le service de protection en temps réel manquait.** Après
  installation, l'application affichait « Service de protection non installé ». Les paquets
  installent maintenant :
  - `/usr/lib/systemd/system/linux-defender-onaccess.service`, **désactivé** : il ne démarre que si
    on l'active ;
  - `/etc/linux-defender/clamonacc.conf`, la configuration de `clamonacc` (fichier de
    configuration conservé lors des mises à jour) ;
  - `/etc/logrotate.d/linux-defender`, la rotation du journal.
- Les scripts d'installation relancent la lecture des services par systemd (`daemon-reload`) :
  le service est visible tout de suite, sans redémarrage. À la désinstallation, il est arrêté ;
  lors d'une mise à jour, il redémarre seulement s'il tournait déjà.
- L'application voit le service apparaître ou changer d'état sans redémarrer (signaux
  `Reloading` et `UnitFilesChanged` de systemd).
- Le message « Service de protection non installé » affirmait que les paquets fournissaient le
  service alors que ce n'était pas le cas ; il précise maintenant que l'archive `.tar.gz` ne le
  contient pas.

### Ajouté

- Protection en temps réel (supervision de `clamonacc`) : état du service, diagnostic des
  erreurs, notifications et liste des détections.
- Scan à la demande de fichiers et de dossiers (FILDES), scan automatique des clés USB, icône
  dans la zone de notification, instance unique, démarrage automatique, paramètres.
- Paquets `.deb` et `.rpm`, archive `.tar.gz`, intégration continue GitHub Actions.
