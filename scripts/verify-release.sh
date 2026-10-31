#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

fail() {
    echo "verify-release: FAIL: $*" >&2
    exit 1
}

ok() {
    echo "verify-release: ok: $*"
}

if [[ ! -d build ]]; then
    llvm_dir=""
    if [[ "$(uname -s)" == "Darwin" ]]; then
        llvm_dir="$(brew --prefix llvm)/lib/cmake/llvm"
    else
        llvm_dir="$(llvm-config --cmakedir)"
    fi
    cmake -G Ninja -S . -B build -DLLVM_DIR="$llvm_dir"
fi

cmake --build build
ctest --test-dir build --output-on-failure

[[ -x build/yukt ]] || fail "build/yukt is missing or not executable"

version="$(build/yukt --version)"
echo "$version" | grep -q "0.14.1" || fail "yukt --version is '$version', expected 0.14.1"
ok "yukt --version is $version"

file_out="$(file build/yukt)"
echo "$file_out"
case "$(uname -s)" in
    Darwin)
        echo "$file_out" | grep -q "Mach-O" || fail "build/yukt is not Mach-O: $file_out"
        echo "$file_out" | grep -qi "executable" || fail "build/yukt is not an executable: $file_out"
        ;;
    Linux)
        echo "$file_out" | grep -q "ELF" || fail "build/yukt is not ELF: $file_out"
        echo "$file_out" | grep -qi "executable" || fail "build/yukt is not an executable: $file_out"
        ;;
    *)
        fail "unsupported host $(uname -s)"
        ;;
esac
ok "file(1) matches the host"

frontend_dirs=(
    compiler/lexer
    compiler/parser
    compiler/ast
    compiler/semantic
    compiler/driver
    compiler/common
    compiler/ir
)
llvm_hits="$(grep -R -n -E '#include[[:space:]]*[<"]llvm/' "${frontend_dirs[@]}" || true)"
if [[ -n "$llvm_hits" ]]; then
    echo "$llvm_hits" >&2
    fail "frontend directories include LLVM headers"
fi
ok "frontend directories have no LLVM includes"

grep -q "8948352" runtime/yukt_runtime.c || fail "allocator cap 8948352 missing from runtime/yukt_runtime.c"
grep -q "Six classes" runtime/yukt_runtime.h || fail "six size classes missing from runtime/yukt_runtime.h"
grep -q "Four slots" runtime/yukt_runtime.h || fail "four slots missing from runtime/yukt_runtime.h"
for class in 16 64 256 4096 65536 1048576; do
    grep -q "$class" runtime/yukt_runtime.h || fail "size class $class missing from runtime/yukt_runtime.h"
done
ok "allocator cap / six classes / four slots present"

grep -q "1000000" compiler/ir/lower.cpp || fail "parallel trip threshold 1000000 missing from compiler/ir/lower.cpp"
grep -q "100000 \* ncpu" runtime/yukt_runtime.c || fail "runtime tax 100000 * ncpu missing from runtime/yukt_runtime.c"
ok "parallel thresholds 1000000 and 100000 * ncpu present"

if ! git diff --quiet HEAD -- benchmarks/results-0.11.json; then
    git diff -- benchmarks/results-0.11.json >&2
    fail "benchmarks/results-0.11.json differs from HEAD"
fi
ok "benchmarks/results-0.11.json unchanged vs HEAD"

expect_tag() {
    local tag="$1"
    local expected="$2"
    local got
    got="$(git rev-parse "refs/tags/$tag")"
    if [[ "$got" != "$expected" ]]; then
        fail "$tag moved: expected $expected, got $got"
    fi
    ok "$tag is $got"
}

expect_tag v0.11.0 ef0e2fb043d3c1a589aff6c61c678175331cc7d9
expect_tag v0.12.0 6a961e67b36cde45079abc6203c7caac5c466e20
expect_tag v0.12.1 3b600754a36828b4e9f225591bcd1878a31f5c85
expect_tag v0.13.1 fbc6ff80dc4296ad22e6cbd86b0402d16da97fd6

echo "verify-release: all checks passed"
