#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
test -x "$ROOT/scripts/package.sh"
BUILD="${YUKT_BUILD_DIR:-$ROOT/build}"
TAR="$(YUKT_BUILD_DIR="$BUILD" "$ROOT/scripts/package.sh")"
STAGE="$(mktemp -d "${TMPDIR:-/tmp}/yukt-smoke.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT
tar -xzf "$TAR" -C "$STAGE"
PATH_BASE="/usr/bin:/bin:/usr/sbin:/sbin"
if [[ -d /Library/Developer/CommandLineTools/usr/bin ]]; then
    PATH_BASE="$PATH_BASE:/Library/Developer/CommandLineTools/usr/bin"
fi
env -i HOME="$HOME" PATH="$PATH_BASE" "$STAGE/bin/yukt" --version
