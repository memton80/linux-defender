#!/bin/sh
# Construit le paquet .deb dans dist/. À lancer à la racine du dépôt, sur
# Debian ou Ubuntu, avec les dépendances de construction installées :
#   sudo apt install dpkg-dev debhelper cmake qt6-base-dev dbus-daemon
# Les tests sont relancés pendant la construction (sauf DEB_BUILD_OPTIONS=nocheck).
set -eu

version=$(packaging/version.sh --package)

# dpkg-buildpackage attend le dossier debian/ à la racine des sources.
rm -rf debian
cp -r packaging/debian debian
trap 'rm -rf debian obj-*' EXIT  # obj-* : dossier de build de debhelper

# Version du paquet : première ligne de debian/changelog.
sed -i "1s/([^)]*)/($version)/" debian/changelog

# -b : paquet binaire seulement ; -us -uc : pas de signature.
dpkg-buildpackage -b -us -uc

mkdir -p dist
mv "../linux-defender_${version}_"*.deb dist/
rm -f "../linux-defender_${version}_"*.buildinfo "../linux-defender_${version}_"*.changes \
      "../linux-defender-dbgsym_${version}_"*.ddeb "../linux-defender-dbgsym_${version}_"*.deb
ls -l dist/*.deb
