#!/bin/sh
# Affiche la version de l'application : celle du fichier VERSION, à la racine
# du dépôt (seul endroit où la changer).
#
# Sur un tag git vX.Y.Z (ou vX.Y.Z-suffixe), la version vient du tag, qui doit
# correspondre au fichier VERSION : une release ne peut pas porter un autre
# numéro que celui affiché par l'application. Sur GitHub Actions, le tag est lu
# dans GITHUB_REF_TYPE / GITHUB_REF_NAME ; en local, dans git (commit courant
# exactement sur un tag).
#
# Option --package : même version, avec « ~ » à la place de « - », comme
# l'exigent les paquets (1.0.0-rc1 devient 1.0.0~rc1). Le « ~ » classe la
# version avant la version finale : 1.0.0~rc1 < 1.0.0.
set -eu

root=$(dirname "$0")/..
file_version=$(head -n 1 "$root/VERSION" | tr -d '[:space:]')
case "$file_version" in
    [0-9]*.[0-9]*.[0-9]*) ;;
    *) echo "version.sh : le fichier VERSION doit contenir X.Y.Z (lu : « $file_version »)" >&2; exit 1 ;;
esac

if [ "${GITHUB_REF_TYPE:-}" = "tag" ]; then
    tag="${GITHUB_REF_NAME:-}"
else
    tag=$(git -C "$root" describe --tags --exact-match 2>/dev/null || true)
fi

version="$file_version"
case "$tag" in
    v[0-9]*.[0-9]*.[0-9]*)
        version="${tag#v}"
        case "$version" in
            "$file_version" | "$file_version"-*) ;;
            *) echo "version.sh : le tag $tag ne correspond pas au fichier VERSION ($file_version)" >&2; exit 1 ;;
        esac
        ;;
esac

if [ "${1:-}" = "--package" ]; then
    echo "$version" | tr '-' '~'
else
    echo "$version"
fi
