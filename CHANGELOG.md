# Journal des modifications

Les versions publiées correspondent aux tags `vX.Y.Z` (voir les
[releases](https://github.com/memton80/linux-defender/releases)). Les paquets construits hors tag
portent la version `0.0.0~dev`.

## [Non publié]

### Corrigé

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
