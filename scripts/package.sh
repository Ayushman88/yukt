#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${YUKT_BUILD_DIR:-$ROOT/build}"
STAGE="${YUKT_STAGE:-$(mktemp -d "${TMPDIR:-/tmp}/yukt-stage.XXXXXX")}"
DEST="${YUKT_DIST:-$ROOT/dist}"
KEEP_STAGE="${YUKT_KEEP_STAGE:-0}"

if [[ ! -d "$BUILD" ]]; then
    echo "package.sh: build directory not found: $BUILD" >&2
    exit 1
fi

cmake --install "$BUILD" --prefix "$STAGE" >&2
mkdir -p "$STAGE/lib"

is_system_lib() {
    case "$1" in
        /usr/lib/*|/usr/lib64/*|/lib/*|/lib64/*|/System/*)
            return 0
            ;;
        linux-vdso.so*|ld-linux*|*libc.so*|*libm.so*|*libpthread.so*|*libdl.so*|*libgcc_s.so*|*libstdc++.so*|*libc++.1.dylib|*libSystem.B.dylib|*libz.1.dylib)
            return 0
            ;;
    esac
    return 1
}

list_deps() {
    local file="$1"
    if [[ "$(uname -s)" == Darwin ]]; then
        otool -L "$file" | awk 'NR>1 {print $1}'
    else
        ldd "$file" 2>/dev/null | awk '
            /=>/ { if ($3 != "") print $3; next }
            /^\t\// { print $1 }
        '
    fi
}

copy_closure() {
    local pending=("$STAGE/bin/yukt")
    while ((${#pending[@]} > 0)); do
        local file="${pending[0]}"
        pending=("${pending[@]:1}")
        while IFS= read -r dep; do
            [[ -z "$dep" || "$dep" == "not" ]] && continue
            if is_system_lib "$dep"; then
                continue
            fi
            if [[ "$dep" == @* ]]; then
                continue
            fi
            if [[ ! -f "$dep" ]]; then
                continue
            fi
            local base
            base="$(basename "$dep")"
            local dest="$STAGE/lib/$base"
            if [[ ! -f "$dest" ]]; then
                cp "$dep" "$dest"
                pending+=("$dest")
            fi
        done < <(list_deps "$file")
    done
}

rewrite_macos() {
    local file="$1"
    local loader
    if [[ "$file" == *"/bin/"* ]]; then
        loader="@loader_path/../lib"
    else
        loader="@loader_path"
    fi
    while IFS= read -r dep; do
        [[ -z "$dep" ]] && continue
        if is_system_lib "$dep"; then
            continue
        fi
        if [[ "$dep" == @* ]]; then
            continue
        fi
        local base
        base="$(basename "$dep")"
        if [[ -f "$STAGE/lib/$base" ]]; then
            install_name_tool -change "$dep" "$loader/$base" "$file"
        fi
    done < <(list_deps "$file")
    if [[ "$file" == "$STAGE/bin/yukt" ]]; then
        install_name_tool -add_rpath "@loader_path/../lib" "$file" 2>/dev/null || true
    else
        install_name_tool -id "$loader/$(basename "$file")" "$file"
    fi
    if command -v codesign >/dev/null 2>&1; then
        codesign --force -s - "$file" >/dev/null 2>&1 || true
    fi
}

rewrite_linux() {
    local file="$1"
    local rpath
    if [[ "$file" == *"/bin/"* ]]; then
        rpath='$ORIGIN/../lib'
    else
        rpath='$ORIGIN'
    fi
    if command -v patchelf >/dev/null 2>&1; then
        patchelf --set-rpath "$rpath" "$file"
    fi
}

copy_closure

if [[ "$(uname -s)" == Darwin ]]; then
    rewrite_macos "$STAGE/bin/yukt"
    for lib in "$STAGE/lib"/*.dylib; do
        [[ -e "$lib" ]] || continue
        rewrite_macos "$lib"
    done
else
    rewrite_linux "$STAGE/bin/yukt"
    for lib in "$STAGE/lib"/*.so*; do
        [[ -e "$lib" ]] || continue
        [[ "$lib" == *.a ]] && continue
        rewrite_linux "$lib"
    done
fi

mkdir -p "$DEST"
VERSION="$("$STAGE/bin/yukt" --version | awk '{print $2}')"
OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"
TAR="$DEST/yukt-${VERSION}-${OS}-${ARCH}.tar.gz"
tar -C "$STAGE" -czf "$TAR" .
echo "$TAR"

if [[ "$KEEP_STAGE" != 1 ]]; then
    rm -rf "$STAGE"
fi
