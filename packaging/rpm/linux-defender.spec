# Paquet RPM (Fedora). Construit par packaging/build-rpm.sh, qui passe la
# version de l'application (fichier VERSION) : --define "app_version 1.2.3".
%{!?app_version:%{error:app_version manquante : construire le paquet avec packaging/build-rpm.sh}}

Name:           linux-defender
# « - » est interdit dans la version d'un RPM : 1.0.0-rc1 devient 1.0.0~rc1
# (le « ~ » classe la version avant la version finale 1.0.0).
Version:        %{lua: print((string.gsub(rpm.expand("%{app_version}"), "[-]", "~")))}
Release:        1%{?dist}
Summary:        Lightweight Qt interface for the ClamAV antivirus
Summary(fr):    Interface Qt légère pour l'antivirus ClamAV

# Aucune licence n'a encore été choisie par l'auteur : tous droits réservés.
License:        LicenseRef-AllRightsReserved
URL:            https://github.com/memton80/linux-defender
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.21
BuildRequires:  gcc-c++
BuildRequires:  qt6-qtbase-devel >= 6.4
# Vérification du fichier .desktop et tests (bus D-Bus privé) dans %%check.
BuildRequires:  desktop-file-utils
BuildRequires:  dbus-daemon
# Macros %%{_unitdir} et %%systemd_post (service de protection en temps réel).
BuildRequires:  systemd-rpm-macros

# Plugin SVG de Qt, chargé à l'exécution pour les icônes : non détecté automatiquement.
Requires:       qt6-qtsvg%{?_isa}
Requires:       hicolor-icon-theme
# clamd peut aussi tourner ailleurs (socket configurable) : simple recommandation.
Recommends:     clamd
Recommends:     clamav-update
Recommends:     udisks2
# pkexec : corrections du diagnostic et case de la protection en temps réel.
Recommends:     polkit

%description
Linux Defender is a native Qt 6 (QtWidgets) front-end for the ClamAV daemon
(clamd), designed for KDE Plasma and its Breeze theme: dashboard, quick, full
and custom scans, automatic scan of USB keys when they are mounted, scan
history, quarantine, real-time protection (clamonacc, enabled at installation), system tray
icon showing the clamd status, with desktop notifications.

Files are opened by the application and passed to clamd (FILDES), so clamd can
scan the home directory and removable media without reading them itself.

%description -l fr
Linux Defender est une interface Qt 6 (QtWidgets) native pour le démon de
ClamAV (clamd), pensée pour KDE Plasma et son thème Breeze : tableau de bord,
analyses rapide, complète ou personnalisée, analyse automatique des clés USB au
montage, historique des analyses, quarantaine, protection en temps réel (clamonacc, activée
à l'installation), icône dans la zone de notification avec l'état de clamd et
des notifications.

Les fichiers sont ouverts par l'application puis transmis à clamd (FILDES) :
clamd peut ainsi analyser le dossier personnel et les supports amovibles sans
avoir le droit de les lire lui-même.

%prep
%autosetup -n %{name}-%{version}

%build
# Protection en temps réel, valeurs propres à Fedora : clamd tourne via
# clamd@scan, sous l'utilisateur clamscan ; clamonacc est dans %%{_sbindir}
# (/usr/bin depuis la fusion de /usr/sbin dans /usr/bin).
%cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DDEFENDER_VERSION=%{app_version} \
       -DDEFENDER_CLAMONACC=%{_sbindir}/clamonacc \
       -DDEFENDER_CLAMD_SOCKET=/run/clamd.scan/clamd.sock \
       -DDEFENDER_CLAMD_USER=clamscan \
       -DDEFENDER_SYSTEMD_UNIT_DIR=%{_unitdir}
%cmake_build

%install
%cmake_install
# Règle de préréglage (preset) : la protection en temps réel est activée à
# l'installation. La règle par défaut de Fedora (90-default.preset,
# « disable * ») désactive les services inconnus ; celle-ci, numérotée avant,
# l'emporte. %%systemd_post l'applique à la première installation seulement.
mkdir -p %{buildroot}%{_presetdir}
echo 'enable linux-defender-onaccess.service' > %{buildroot}%{_presetdir}/80-linux-defender.preset

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/%{name}.desktop
%ctest

%post
# Service de protection en temps réel : activé à la première installation
# (préréglage 80-linux-defender.preset), puis démarré.
%systemd_post linux-defender-onaccess.service
# systemd relit ses fichiers pour voir le service tout de suite.
if [ -d /run/systemd/system ]; then
    systemctl daemon-reload >/dev/null 2>&1 || :
    if [ "$1" -eq 1 ]; then
        systemctl start linux-defender-onaccess.service >/dev/null 2>&1 || :
    fi
fi
# Première installation seulement : rappel des réglages nécessaires côté clamd.
if [ "$1" -eq 1 ]; then
    cat <<'EOF'

Linux Defender est installé. Sous Fedora, activez d'abord le socket de clamd :
décommentez « LocalSocket /run/clamd.scan/clamd.sock » dans
/etc/clamd.d/scan.conf, puis : sudo systemctl enable --now clamd@scan
Si l'application signale « Permission refusée », ajoutez votre utilisateur au
groupe qu'elle indique (en général « virusgroup »), puis reconnectez-vous :
    sudo usermod -aG virusgroup $USER
La protection en temps réel (linux-defender-onaccess.service) est activée ;
pour la désactiver :
    sudo systemctl disable --now linux-defender-onaccess.service
Détails : https://github.com/memton80/linux-defender#configurer-clamd

EOF
fi

%preun
# Désinstallation : la protection en temps réel est arrêtée et désactivée.
%systemd_preun linux-defender-onaccess.service

%postun
# Mise à jour : si elle tournait, elle redémarre avec la nouvelle version.
%systemd_postun_with_restart linux-defender-onaccess.service

%triggerun -- linux-defender < 1.0.2
# Mise à jour depuis une version antérieure à 1.0.2 : le service y était
# installé désactivé d'office. Il est activé et démarré une fois, comme lors
# d'une première installation ; ensuite, le choix de l'administrateur est respecté.
systemctl --no-reload preset linux-defender-onaccess.service >/dev/null 2>&1 || :
if [ -d /run/systemd/system ]; then
    systemctl daemon-reload >/dev/null 2>&1 || :
    systemctl start linux-defender-onaccess.service >/dev/null 2>&1 || :
fi

%files
%doc README.md
%{_bindir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/scalable/apps/%{name}.svg
# Menu contextuel de Dolphin ; dossiers possédés aussi : KDE n'est pas requis.
%dir %{_datadir}/kio
%dir %{_datadir}/kio/servicemenus
%{_datadir}/kio/servicemenus/%{name}-scan.desktop
%{_mandir}/man1/%{name}.1*
%{_unitdir}/linux-defender-onaccess.service
%{_presetdir}/80-linux-defender.preset
%dir %{_sysconfdir}/linux-defender
%config(noreplace) %{_sysconfdir}/linux-defender/clamonacc.conf
%config(noreplace) %{_sysconfdir}/logrotate.d/linux-defender
%{_libexecdir}/linux-defender-helper
%{_datadir}/polkit-1/actions/io.github.memton80.linux-defender.policy

%changelog
* Thu Sep 24 2026 memton80 <memton80@users.noreply.github.com> - 1.0.2-1
- Nouvelle interface : tableau de bord, historique, paramètres en pages.
- Protection en temps réel activée et démarrée à l'installation.
- Détails : CHANGELOG.md.
