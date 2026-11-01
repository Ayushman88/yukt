# AGENTS.md

This file is the working contract for humans and AI assistants contributing to YUKT.

YUKT is a real programming language. The compiler is handwritten C++20. Do not implement YUKT as a Python wrapper, transpiler, or interpreter disguised as a compiler.

## Current program

**0.15 — stabilization (in progress).** 0.14.1 is released as `v0.14.1`. 0.13.1–0.13.6 remain on `main` (`v0.13.1` local; `v0.13.6` = `eb42a0a`). 0.12 remains `v0.12.1`. 0.11 remains `v0.11.0`.

0.15 adds **no** language features. Finish runtime correctness, documentation, and benchmark **classification**. **Performance freeze:** no kernel tuning unless a real regression appears. Sequential matmul codegen parity is established; do not continue the matmul investigation.

**0.16–0.18 is one development sprint** with three internally validated phases. The milestone names stay. Do not implement a whole version in one task. One engineering objective at a time, with an acceptance gate. Independent work may proceed in parallel; **do not start the next dependent subsystem until the current one passes its tests.** Specified is not implemented. A design sketch is not supported syntax. **0.16.1 growing lists and 0.16.2 stdin / argv / environment are implemented.**

| Phase | Milestone | Work | Gate |
| --- | --- | --- | --- |
| 1 | **0.16** Practical language | Growing lists (0.16.1), then stdin / env, then richer strings | Spec → plan → implement → tests. Ownership and memory-safety tests required |
| 2 | **0.17** Systems | Mutexes / channels, sockets, HTTP client and server | Spec first. Concurrency and networking stress tests |
| 3 | **0.18** Developer experience | REPL, packages, formatter, language server, project create/build/deps | Spec first. Clean install, build, and error paths |

**Finish line:** a developer can comfortably build a CLI, a JSON API client, an HTTP server, and a concurrent file processor. More keywords are not the product. 0.19 (Windows / cross-compile) and GPU stay outside this sprint. GPU stays off the path to 1.0.

**Next authorized objective:** specify richer strings in `docs/LANGUAGE_SPEC.md` / `docs/STDLIB_SPEC.md`, then implement them. Not HTTP, mutexes, or 0.18 tooling.

Rules that still hold for every objective:

1. Copy the surface into `docs/LANGUAGE_SPEC.md` (and `docs/STDLIB_SPEC.md` when it is an API) **before** compiler or runtime work.
2. Write an implementation plan after the spec is frozen, **before** code.
3. Implement frontend, IR, backend, runtime, and stdlib only as that plan names them.
4. Every feature needs tests under `tests/`. Freeze the feature only after those tests pass on macOS arm64 and Linux x86-64 CI.
5. Do not invent syntax. Do not implement unspecified features.

**0.16.1 growing lists** are specified in `docs/LANGUAGE_SPEC.md` and `docs/YUKT_016_DESIGN.md` (D1–D11 frozen) and implemented (`list.append` / `list.remove` / `list.pop`). **0.16.2 stdin / argv / environment** are specified and implemented (`std/io`, `std/env`). Richer strings are unspecified. Mutex / HTTP are **0.17**. REPL / packages / LSP are **0.18**.

Implemented: lexer, parser, AST, diagnostics (notes, GNU line, TTY color, per-file locations), scopes, type inference, YUKT IR, LLVM lowering, host object emission, `yukt build`, `yukt run`, `yukt --explain`, LLVM O3, list indexing, `count` / `zeros`, work-based automatic parallelization, simple +/* parallel reductions, adaptive runtime parallelization, host-CPU SIMD via LLVM vectorize, counted-loop lowering, range-parallel runtime, aligned list payloads, size-class payload reuse, host-native CI (macOS arm64, Linux x86-64), the 0.08–0.10 performance sprints, the 0.12 module system (`use` / `share` / `as` / `.` qualification, whole-program DAG, one native executable), the 0.12.1 now-gated standard library (`std/str`, `std/list`, `std/sort`, `std/math`, `std/stat`), 0.13.1 records, 0.13.2 ownership, 0.13.3 mutable `ref` parameters, 0.13.4 `foreign` C, 0.13.5 `result[T]` / `ok` / `fail`, 0.13.6 `std/map`, `std/set`, `std/file`, `std/json`, 0.14.1 `start` / `wait` / `task[T]` on the host pool, 0.16.1 growing lists, and 0.16.2 `std/io` / `std/env`.

Not implemented: richer strings, `std/array`, mutex / channels, HTTP / sockets, package management, REPL, formatter, language server, cross-compilation to non-host triples, or CUDA / GPU. Do not overwrite `benchmarks/results-0.11.json`. 0.13 design: `docs/YUKT_013_DESIGN.md`. 0.14 design: `docs/YUKT_014_DESIGN.md`. 0.15 design: `docs/YUKT_015_DESIGN.md`. 0.16 design: `docs/YUKT_016_DESIGN.md`.

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
| `std` | Standard library (`str`, `list`, `sort`, `math`, `stat`, `map`, `set`, `file`, `json`, `io`, `env`) | runtime where YUKT cannot express the op |

## Language constraints

YUKT 0.14.1 is indentation-sensitive. Blocks are introduced by `:` and a newline, then an indented suite.

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
- `list.append` / `list.remove` / `list.pop` (growing lists; `use std/list`)
- `io.program` / `io.args` / `io.read_line` / `io.read_all` (`use std/io`)
- `env.get` / `env.get_or` / `env.set` / `env.has` (`use std/env`)
- `ref T` parameters (`create increment(value: ref integer):`)
- `start` / `wait` and owned `task[T]`
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

## Out of scope until their phase and spec are ready

- Growing-list **code** until the 0.16.1 implementation plan exists and is followed (spec is frozen)
- Richer strings until they are in `docs/LANGUAGE_SPEC.md` and have their own plan
- Mutex, channels, HTTP, sockets, async until 0.17 is specified (not in a 0.16 task)
- Package management, REPL, formatter, language server until 0.18 is specified
- `std/array` (distinct type; not an alias of `list`)
- Windows target and cross-compilation to non-host triples (0.19; outside this sprint)
- CUDA / GPU (off the path to 1.0; Apple Silicon has no CUDA)
- Implementing several phases in one task
- Further benchmark-kernel optimization unless a real regression appears
