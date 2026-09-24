#!/bin/sh
# Affiche la version de l'application :
#   - tag git vX.Y.Z (ou vX.Y.Z-suffixe) : X.Y.Z (ou X.Y.Z-suffixe) ;
#   - sinon : 0.0.0-dev.
# Sur GitHub Actions, le tag est lu dans GITHUB_REF_TYPE / GITHUB_REF_NAME ;
# en local, dans git (commit courant exactement sur un tag).
#
# Option --package : même version, avec « ~ » à la place de « - », comme
# l'exigent les paquets (0.0.0-dev devient 0.0.0~dev). Le « ~ » classe la
# version avant la version finale : 1.0.0~rc1 < 1.0.0.
set -eu

if [ "${GITHUB_REF_TYPE:-}" = "tag" ]; then
    tag="${GITHUB_REF_NAME:-}"
else
    tag=$(git describe --tags --exact-match 2>/dev/null || true)
fi

case "$tag" in
    v[0-9]*.[0-9]*.[0-9]*) version="${tag#v}" ;;
    *) version="0.0.0-dev" ;;
esac

if [ "${1:-}" = "--package" ]; then
    echo "$version" | tr '-' '~'
else
    echo "$version"
fi
