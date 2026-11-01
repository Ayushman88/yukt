#!/usr/bin/env bash
set -euo pipefail

PREFIX="${PREFIX:-$HOME/.yukt}"
TARBALL="${1:-}"

if [[ -z "$TARBALL" ]]; then
    echo "Usage: PREFIX=/path $0 <yukt-*.tar.gz>" >&2
    echo "Installs YUKT into PREFIX (default: $HOME/.yukt) and prints a PATH export." >&2
    exit 1
fi

if [[ "$TARBALL" != "-" && ! -f "$TARBALL" ]]; then
    echo "install.sh: tarball not found: $TARBALL" >&2
    exit 1
fi

STAGE="${PREFIX}.stage.$$"
BACKUP="${PREFIX}.bak.$$"
cleanup() {
    rm -rf "$STAGE"
}
trap cleanup EXIT

mkdir -p "$STAGE"
if [[ "$TARBALL" == "-" ]]; then
    tar -xzf - -C "$STAGE"
else
    tar -xzf "$TARBALL" -C "$STAGE"
fi

if [[ ! -x "$STAGE/bin/yukt" ]]; then
    echo "install.sh: unpacked tree is missing bin/yukt" >&2
    exit 1
fi
"$STAGE/bin/yukt" --version >/dev/null

if [[ -e "$PREFIX" ]]; then
    mv "$PREFIX" "$BACKUP"
fi
if ! mv "$STAGE" "$PREFIX"; then
    if [[ -e "$BACKUP" ]]; then
        rm -rf "$PREFIX"
        mv "$BACKUP" "$PREFIX"
    fi
    echo "install.sh: could not move the new prefix into place" >&2
    exit 1
fi
trap - EXIT
rm -rf "$BACKUP"

echo "Installed YUKT to $PREFIX"
echo "Add it to your PATH:"
echo "  export PATH=\"$PREFIX/bin:\$PATH\""
