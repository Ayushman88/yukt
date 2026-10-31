# AGENTS.md

This file is the working contract for humans and AI assistants contributing to YUKT.

YUKT is a real programming language. The compiler is handwritten C++20. Do not implement YUKT as a Python wrapper, transpiler, or interpreter disguised as a compiler.

## Current milestone

**0.14 — planning** (0.13.1–0.13.6 are on `main` at `5463e7f`; 0.13.1 records are tagged `v0.13.1` locally, not pushed; 0.12 remains tagged `v0.12.1`; 0.11 remains tagged `v0.11.0`. There is no `v0.13.2` / `v0.13.6` tag).

Implemented: lexer, parser, AST, diagnostics (notes, GNU line, TTY color, per-file locations), scopes, type inference, YUKT IR, LLVM lowering, host object emission, `yukt build`, `yukt run`, `yukt --explain`, LLVM O3, list indexing, `count` / `zeros`, work-based automatic parallelization, simple +/* parallel reductions, adaptive runtime parallelization, host-CPU SIMD via LLVM vectorize, counted-loop lowering, range-parallel runtime, aligned list payloads, size-class payload reuse, host-native CI (macOS arm64, Linux x86-64), the 0.08–0.10 performance sprints, the 0.12 module system (`use` / `share` / `as` / `.` qualification, whole-program DAG, one native executable), the 0.12.1 now-gated standard library (`std/str`, `std/list`, `std/sort`, `std/math`, `std/stat`), 0.13.1 records, 0.13.2 ownership, 0.13.3 mutable `ref` parameters, 0.13.4 `foreign` C, 0.13.5 `result[T]` / `ok` / `fail`, and 0.13.6 `std/map`, `std/set`, `std/file`, `std/json`. Linux x86-64 CI has not been re-run for records through 0.13.6.

Not implemented: `std/array`, package management, cross-compilation to non-host triples, or 0.14 concurrency/GPU. 0.13 design: `docs/YUKT_013_DESIGN.md`.

Native execution is real when `yukt build` writes a host binary and `file` reports an executable for that architecture. Do not call a program "native" if it was interpreted.

## Architecture rules

1. Keep the frontend independent of LLVM. `compiler/lexer`, `compiler/parser`, `compiler/ast`, and `compiler/semantic` must not include LLVM headers.
2. Work module by module. Prefer small, focused files over a single large compilation unit.
3. Do not invent syntax. If a construct is not in `docs/LANGUAGE_SPEC.md`, either reject it or document it there first.
4. Never silently ignore invalid syntax. Report a diagnostic with file, line, and column.
5. Use C++20 ownership types (`std::unique_ptr`, `std::vector`). Do not add raw owning pointers.
6. Do not add dependencies unless the milestone requires them. No parser generators. LLVM stays inside `compiler/backend`.
7. Every language feature needs tests under `tests/`.
8. Keep the build working on Apple Silicon macOS and portable to Linux. Host-native CI must stay green.

## How to build

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

The compiler entry point is `compiler/main.cpp`. Frontend/IR code lives in `yukt_compiler`. LLVM code lives in `yukt_backend`.

## Module map

| Directory | Responsibility | Depends on |
| --- | --- | --- |
| `compiler/common` | Source locations and file text | Nothing |
| `compiler/diagnostics` | Error and warning reporting | common |
| `compiler/lexer` | Tokens, including `INDENT` / `DEDENT` | diagnostics |
| `compiler/ast` | Syntax tree nodes | lexer tokens |
| `compiler/parser` | Recursive-descent parser | lexer, ast, diagnostics |
| `compiler/driver` | File loading, module graph, frontend orchestration | lexer, parser |
| `compiler/semantic` | Types, scopes, inference | ast, diagnostics |
| `compiler/ir` | YUKT IR and AST lowering | semantic |
| `compiler/backend` | LLVM lowering, objects, linking | ir |
| `runtime` | C runtime linked into generated programs | — |
| `std` | Standard library (`str`, `list`, `sort`, `math`, `stat`, `map`, `set`, `file`, `json`) | runtime where YUKT cannot express the op |

## Language constraints

YUKT 0.13.6 is indentation-sensitive. Blocks are introduced by `:` and a newline, then an indented suite.

Supported syntax is listed in `docs/LANGUAGE_SPEC.md`:

- assignments, list index assignment, and record field assignment
- integer, float, boolean, and string literals
- arithmetic and comparison operators
- `create` functions and `share create` exports
- `record` / `share record` declarations and positional constructors
- `ref` parameters, `foreign` C declarations, `ok` / `fail` / `result[T]`
- `use path [as alias]` and qualified calls `alias.name(...)`
- `say`, `give`, `if` / `otherwise`, `repeat`
- lists, indexing, `count`, `zeros`, including lists of records
- `ref T` parameters (`create increment(value: ref integer):`)
- `#` comments

Do not add keywords, operators, or statements without updating the spec, tests, and this file.

## Inspection commands

```bash
build/yukt --tokens examples/hello.yukt
build/yukt --ast examples/hello.yukt
build/yukt --types examples/hello.yukt
build/yukt --emit-ir examples/hello.yukt
build/yukt --emit-llvm examples/hello.yukt
build/yukt --explain examples/hello.yukt
build/yukt run examples/hello.yukt
```

If `--ast` fails, fix the parser. If `--types` fails, fix semantic analysis. If `--emit-ir` is wrong, fix lowering. If `--emit-llvm` or `run` is wrong, fix the backend or runtime. Do not compensate in later passes.

## Implementation style

- Prefer explicit code over clever templates.
- Keep diagnostics actionable: say what was found, why it is wrong, and how to fix it.
- Preserve source locations on tokens and AST nodes.
- When a feature is incomplete, say so in diagnostics and documentation.
- Update `docs/ROADMAP.md` when a milestone actually lands.

## Out of scope until later milestones

- `std/array` (distinct type; not an alias of `list`)
- Package management
- An interactive REPL
- Cross-compilation to non-host triples
- 0.14: explicit tasks, async, thread-safe allocator, CUDA / GPU
