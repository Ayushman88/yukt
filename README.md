# YUKT

YUKT is an English-inspired, statically typed, compiled programming language. This repository contains its compiler, language specification, and tests.

The current release is **0.12.1**. 0.11 remains tagged `v0.11.0`. 0.12.0 is the module-system tag. `yukt build` and `yukt run` produce **host-native** binaries (Apple Silicon macOS and Linux x86-64) optimized at LLVM O3, including multi-file programs linked through `use`. Independent `repeat` loops and simple `+`/`*` reductions may run in parallel when estimated work is large; the runtime can still sequentialize a call that is not worth dispatching. `yukt --explain` reports those decisions only when they appear in generated LLVM IR. See `docs/YUKT_011_DESIGN.md`, `docs/YUKT_012_DESIGN.md`, `docs/EXECUTION_ENGINE.md`, `docs/RUNTIME.md`, and `docs/STDLIB.md`. Shipped library modules: `std/str`, `std/list`, `std/sort`, `std/math`, `std/stat`.

## Example

```yukt
create greet(name):
    say "Hello, " + name

create main:
    name = "World"
    greet(name)

    repeat i in 1..5:
        say i

    say "YUKT is running!"
```

```text
Hello, World
1
2
3
4
5
YUKT is running!
```

## Status

| Component | Status |
| --- | --- |
| Lexer, including indentation | Implemented |
| Recursive-descent parser | Implemented |
| Abstract syntax tree | Implemented |
| Diagnostics | Implemented |
| Semantic analysis and type inference | Implemented |
| YUKT IR | Implemented |
| LLVM backend | Implemented |
| Native executables (host triple) | Implemented |
| LLVM O3, mem2reg, host CPU | Implemented |
| List index, `count`, `zeros` | Implemented |
| Automatic parallelization | Implemented (conservative + simple +/* reductions) |
| SIMD / loop vectorize | Implemented via LLVM |
| C++ / Rust benches | `benches/` and `docs/BENCHMARKS.md` (per-host) |
| Host-native CI | macOS arm64 and Ubuntu x86-64 |
| Performance engineering | Implemented (0.08); see `docs/PERFORMANCE_ANALYSIS_0.08.md` |
| Compiler optimization | Implemented (0.09); see `docs/PERFORMANCE_ANALYSIS_0.09.md` |
| Intelligent execution engine | Implemented (0.10); see `docs/PERFORMANCE_ANALYSIS_0.10.md` |
| Portability / intelligence | Frozen (0.11); see `docs/YUKT_011_AUDIT.md` |
| Modules (`use` / `share` / `as`) | Implemented (0.12); see `docs/LANGUAGE_SPEC.md` |
| Standard library APIs | `std/str`, `std/list`, `std/sort`, `std/math`, `std/stat` |

`yukt file.yukt` type-checks. `yukt build` and `yukt run` generate native code through LLVM.

## Requirements

- C++20 compiler (`clang++` on macOS)
- C compiler for the runtime
- CMake 3.20+
- Ninja
- LLVM 18+ (Homebrew `llvm` 23 is tested on Apple Silicon; Linux CI uses distro LLVM 18+)

On macOS:

```bash
brew install cmake ninja llvm
xcode-select --install
```

On Ubuntu:

```bash
sudo apt-get install -y cmake ninja-build clang llvm-dev
```

`LLVM_DIR` is typically `$(llvm-config --cmakedir)` or `/usr/lib/llvm-*/lib/cmake/llvm`.

## Build

macOS:

```bash
cmake -G Ninja -S . -B build -DLLVM_DIR="$(brew --prefix llvm)/lib/cmake/llvm"
cmake --build build
ctest --test-dir build --output-on-failure
```

Linux:

```bash
cmake -G Ninja -S . -B build -DLLVM_DIR="$(llvm-config --cmakedir)"
cmake --build build
ctest --test-dir build --output-on-failure
```

The compiler binary is `build/yukt`.

Same-algorithm C++ and Rust benches. The harness times in-process kernel compute (warmup discarded, monotonic clock), not process startup:

```bash
python3 benches/run.py
```

Numbers and the claims that run supports are written to `docs/BENCHMARKS.md`.

## Usage

```bash
build/yukt examples/hello.yukt
build/yukt --emit-ir examples/hello.yukt
build/yukt --emit-llvm examples/hello.yukt
build/yukt --explain examples/hello.yukt
build/yukt build examples/hello.yukt -o hello
build/yukt run examples/hello.yukt
```

## Project layout

```
compiler/    Frontend, YUKT IR, and LLVM backend
runtime/     C runtime linked into generated programs
std/         Standard library modules (`str`, `list`, `sort`, `math`, `stat`)
tests/       CTest suites
examples/    Sample YUKT programs
docs/        Language and compiler documentation
tools/       Reserved for developer tools
```

## Documentation

- [Language specification](docs/LANGUAGE_SPEC.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Roadmap](docs/ROADMAP.md)
- [Contributor / agent guide](AGENTS.md)
- [Benchmarks vs C++ and Rust](docs/BENCHMARKS.md)
- [0.10 design](docs/YUKT_010_DESIGN.md)
- [0.11 design](docs/YUKT_011_DESIGN.md)
- [0.11 freeze audit](docs/YUKT_011_AUDIT.md)
- [0.12 design](docs/YUKT_012_DESIGN.md)
- [0.13 design](docs/YUKT_013_DESIGN.md)
- [Execution engine](docs/EXECUTION_ENGINE.md)
- [Standard library foundations](docs/STDLIB.md)
- [Standard library API contract](docs/STDLIB_SPEC.md)
- [Standard library implementation report](docs/STDLIB_IMPLEMENTATION_REPORT.md)
- [0.10 performance analysis](docs/PERFORMANCE_ANALYSIS_0.10.md)
