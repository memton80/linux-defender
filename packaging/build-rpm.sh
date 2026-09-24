#!/bin/sh
# Construit le paquet .rpm dans dist/. À lancer à la racine du dépôt, sur
# Fedora, avec les dépendances de construction installées :
#   sudo dnf install rpm-build cmake gcc-c++ qt6-qtbase-devel desktop-file-utils dbus-daemon
# Les tests sont relancés pendant la construction (section %check du fichier spec).
# Les arguments éventuels sont transmis à rpmbuild.
set -eu

app_version=$(packaging/version.sh)
version=$(packaging/version.sh --package)
topdir="${RPMBUILD_TOPDIR:-$HOME/rpmbuild}"

# Archive des sources attendue par le fichier spec : linux-defender-<version>/...
mkdir -p "$topdir/SOURCES"
tar --exclude=./.git --exclude=./dist --exclude='./build*' --exclude=./debian \
    --transform "s,^\.,linux-defender-$version," \
    -czf "$topdir/SOURCES/linux-defender-$version.tar.gz" .

rpmbuild -bb --define "_topdir $topdir" --define "app_version $app_version" "$@" \
    packaging/rpm/linux-defender.spec

# Seulement le paquet principal (pas les paquets -debuginfo et -debugsource).
mkdir -p dist
cp "$topdir"/RPMS/*/linux-defender-[0-9]*.rpm dist/
ls -l dist/*.rpm
