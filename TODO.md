# Feuille de route détaillée

Liste de travail des prochaines versions, cochée au fur et à mesure (une case = un point livré
dans une branche, testé). Le résumé des versions publiées est dans [CHANGELOG.md](CHANGELOG.md).

## 1. Fiabilité des résultats

Une analyse ne doit jamais présenter comme « sain » un fichier que clamd n'a pas vraiment
analysé, ni comme « menace » ce qui n'est qu'un soupçon.

- [x] Classer les détections de ClamAV : menace (signature), **suspect** (`PUA.*`, `Heuristics.*`
      heuristiques : hameçonnage, exécutable malformé, macros...) et **non analysé**
      (`Heuristics.Encrypted.*` archive chiffrée, `Heuristics.Limits.Exceeded.*` limites de clamd)
- [x] Fichiers plus gros que la limite de clamd (`MaxFileSize`, 2 Go au plus) : clamd répond
      « OK » sans les avoir lus. Lire la limite dans la configuration de clamd et les marquer
      « non analysé » au lieu de « sain »
- [x] Affichage : statuts, filtres, compteurs, bilan, niveau du bandeau, export CSV
- [x] Historique : compteurs et liste des avertissements enregistrés (fichier compatible avec
      les versions précédentes)
- [x] Notifications : un élément suspect n'est pas une alerte critique ; une archive chiffrée
      détectée en temps réel ne déclenche pas d'alerte
- [x] Tests (classement, limite de clamd, historique), vérification sur un vrai clamd

## 2. Diagnostic et corrections en un clic (polkit)

- [ ] Programme d'aide `linux-defender-helper` lancé par `pkexec`, avec une action polkit
      dédiée (`auth_admin_keep` : un seul mot de passe pour plusieurs corrections). Liste
      fermée d'actions, sans argument libre
- [ ] Page **Diagnostic** : chaque vérification avec son explication, la commande exacte à
      copier et, si possible, un bouton « Corriger »
  - [ ] clamd : service arrêté, en échec, absent ; ligne `Example` et `LocalSocket` de Fedora
  - [ ] accès au socket : groupe à rejoindre, ou session à rouvrir après l'ajout au groupe
  - [ ] signatures obsolètes et service `clamav-freshclam`
  - [ ] SELinux : booléen `antivirus_can_scan_system`
  - [ ] `AlertExceedsMax` (archives analysées en partie par clamd)
  - [ ] protection en temps réel, limite inotify
- [ ] Case **Activer la protection en temps réel** (page Protection en temps réel)
- [ ] Bandeau de l'accueil : « Résoudre » mène au diagnostic
- [ ] Paquets : programme d'aide, action polkit, dépendance à pkexec
- [ ] Tests (évaluation du diagnostic, modifications de configuration par le programme d'aide)

## 3. Intégration au gestionnaire de fichiers

- [ ] Option `--scan <chemins…>` : analyse dans l'instance déjà lancée, ou au lancement
- [ ] Menu contextuel de Dolphin « Analyser avec Linux Defender » (fichiers et dossiers)
- [ ] Action « Analyse rapide » dans le menu des applications (`--quick-scan`)
- [ ] Tests (transmission des chemins à l'instance lancée)

## 4. Analyse rapide renforcée et analyses planifiées

- [ ] Analyse rapide : en plus des dossiers choisis, les emplacements où un programme
      malveillant s'installe (démarrage automatique, services utilisateur, `~/.local/bin`,
      scripts du shell, `/tmp`, `/var/tmp`, `/dev/shm`) et les **programmes en cours
      d'exécution** (y compris ceux dont le fichier a été supprimé du disque)
- [ ] Dossiers partagés (`/tmp`...) : seuls les fichiers de l'utilisateur, sans erreur pour
      ceux des autres
- [ ] Analyses planifiées : quotidienne ou hebdomadaire, rapide ou complète, rattrapée au
      lancement si elle a été manquée, reportée sur batterie
- [ ] Tests

## 5. Quarantaine

- [ ] Mise en quarantaine : fichier retiré de son emplacement, contenu rendu inerte (ni
      exécutable, ni détecté de nouveau par la protection en temps réel), empreinte SHA-256
- [ ] Restauration (sans écraser un fichier existant) et suppression définitive
- [ ] Page **Quarantaine** ; action dans les résultats, les détections en temps réel,
      l'historique et l'alerte d'une détection
- [ ] Tests

## Plus tard

- [ ] Analyses en parallèle (plusieurs connexions à clamd)
- [ ] Dossiers surveillés en temps réel modifiables depuis l'application
- [ ] Bases de signatures tierces (SaneSecurity, URLhaus...)
- [ ] Tableau de bord de sécurité : pare-feu, mises à jour, SELinux/AppArmor, Secure Boot
