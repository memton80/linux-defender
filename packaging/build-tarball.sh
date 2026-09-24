#!/bin/sh
# Crée l'archive du binaire brut dans dist/, à partir d'un dossier de build
# CMake déjà compilé :
#   packaging/build-tarball.sh build
#
# Contenu, organisé comme un préfixe d'installation :
#   bin/linux-defender
#   share/applications/linux-defender.desktop
#   share/icons/hicolor/scalable/apps/linux-defender.svg
#   README.md
# Les autres icônes (état, menaces...) sont intégrées au binaire. Le service
# systemd de la protection en temps réel n'en fait pas partie : il est fourni
# par les paquets .deb et .rpm (composant CMake « onaccess »).
set -eu

build="${1:?Usage : packaging/build-tarball.sh <dossier de build>}"
version=$(packaging/version.sh)
name="linux-defender-${version}-linux-$(uname -m)"
staging="dist/$name"

rm -rf "$staging"
cmake --install "$build" --prefix "$PWD/$staging" --strip --component application
cp README.md "$staging/"
tar -C dist -czf "dist/$name.tar.gz" "$name"
rm -rf "$staging"
ls -l "dist/$name.tar.gz"
