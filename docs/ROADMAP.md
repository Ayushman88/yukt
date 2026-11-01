# YUKT Roadmap

One rule for every milestone: never sacrifice correctness for shorter syntax, and never claim performance that the compiler cannot yet produce.

## 0.01 — Lexer, parser, and AST

**Status: implemented**

- Handwritten C++20 lexer with indentation tokens
- Recursive-descent parser
- AST with source locations
- Diagnostics with file, line, column, explanation, and suggestion
- `yukt --tokens` and `yukt --ast`
- Automated lexer, parser, and program tests

This milestone does **not** execute programs or generate machine code.

## 0.02 — Semantic analysis and type inference

**Status: implemented**

- Name resolution and function/loop scopes
- Type inference for the 0.01 syntax
- Arity and type checks for calls
- Required parameterless `create main`
- Rejection of ill-typed programs that still parse
- `yukt --types` dumps inferred signatures

This milestone still does **not** execute programs or generate machine code.

## 0.03 — LLVM integration

**Status: implemented**

- YUKT IR for functions, locals, arithmetic, calls, lists, `if`, and `repeat`
- Lowering from typed AST to YUKT IR
- LLVM IR emission confined to `compiler/backend`
- `--emit-ir` and `--emit-llvm`

## 0.04 — Native executable generation

**Status: implemented**

- Object emission for the host triple through LLVM
- Linking with the C runtime (`libyukt_runtime.a`)
- `yukt build` writes a native executable
- `yukt run` compiles, links, and executes

Verified on Apple Silicon macOS (`arm64`). Linux uses the same CMake/LLVM path. Host-native CI landed in 0.11.

Benchmarks can start now. Do not claim YUKT is faster than C++ or Rust until those measurements exist.

## 0.05 — Functions, loops, and memory

**Status: implemented**

- Native C calling convention for `yukt_<name>`
- Stack scalars and heap lists/strings
- LLVM O3 (`mem2reg`, LICM, and the rest of the default pipeline)
- List index get/set, `count`, and `zeros`

## 0.06 — Automatic parallelization

**Status: implemented**

- Conservative independence analysis on `repeat`
- Outlined loop bodies and `yukt_parallel_for` (libdispatch on macOS, pthreads on Linux)
- Reductions and loops with `say` / `give` / user calls stay sequential (simple +/* reductions parallelize in 0.11)

## 0.07 — SIMD and advanced computation

**Status: implemented**

- Host-CPU LLVM target, O3 vectorize/SLP
- Unchecked i64/f64 list GEP so numeric loops can vectorize
- Same-algorithm C++ and Rust benches in `benches/`, numbers in `docs/BENCHMARKS.md`

## 0.08 — Performance engineering

**Status: implemented**

- Counted `repeat` lowering (`k < niter`) instead of inclusive `i <= end`
- NSW integer arithmetic, `mustprogress` loop metadata, inline hints
- Range callbacks for `yukt_parallel_for` (one call per CPU chunk, not per index)
- Auto-par cost model: independent loops with a nested `repeat` may parallelize; cheap fills stay sequential
- Heap lists and concatenated strings released when `main` returns
- Persistent thread pool on Linux; libdispatch range apply on macOS
- Measured against the 0.07 compute baseline in `docs/PERFORMANCE_ANALYSIS_0.08.md`

## 0.09 — Compiler optimization and performance validation

**Status: implemented**

- Work-based auto-parallelization (trip × body cost, 1e6 threshold)
- 16-byte aligned list payloads and `llvm.assume` alignment
- Same-LLVM C++ baselines plus sequential and parallel C++/Rust where it applies
- 0.08 kept as the permanent compute baseline
- `docs/PERFORMANCE_ANALYSIS_0.09.md`, `docs/OPTIMIZATION_STRATEGY.md`, `benchmarks/results.json`

## 0.10 — Intelligent Execution Engine

**Status: implemented**

- Adaptive `yukt_parallel_for` with a work-per-iteration hint
- Loop decisions recorded in YUKT IR; `yukt --explain` verifies them against LLVM IR
- Payload reuse after `yukt_main` returns
- Fusion / interchange investigated, not applied as YUKT-IR passes
- 15-run median / IQR / MAD; 0.08 and 0.09 kept as baselines
- `docs/YUKT_010_DESIGN.md`, `docs/EXECUTION_ENGINE.md`, `docs/PERFORMANCE_ANALYSIS_0.10.md`

## 0.11 — Portability and compiler intelligence

**Status: implemented and frozen** (audit: `docs/YUKT_011_AUDIT.md`)

- Host-native GitHub Actions: `macos-latest` (arm64) and `ubuntu-latest` (x86-64)
- Linker prefers the LLVM clang the compiler was built against
- Nested unknown trip cost 256; `count`/`zeros` constant folding in the cost model
- Simple parallel reductions: `acc = acc + expr` and `acc = acc * expr` on i64/f64
- Integer overflow undefined; float reduction association not bitwise stable (specified)
- Allocating loop bodies stay sequential (allocator is not thread-safe)
- Size-class list/string payload reuse (bounded bins)
- Diagnostic notes, GNU first line, TTY color
- `--explain` prints host triple, CPU, and cost-model thresholds
- Stdlib foundations documented in `docs/STDLIB.md` (no new syntax)

This milestone does **not** cross-compile, add modules, or add CUDA.

## 0.12 — Modules (stdlib APIs later)

**Status: module system implemented** (`v0.12.0`). **Now-gated stdlib implemented** (`v0.12.1`).

- `use` / `share` / `as` / `.` qualification
- Whole-program DAG: resolve, load, cycle detection, canonical identity
- Default-private exports; qualified calls only
- One YUKT IR module, one LLVM module, one host-native executable
- `std/str`, `std/list`, `std/sort`, `std/math`, `std/stat` ship as ordinary modules
- 0.11 tests remain green; `v0.11.0` is not rewritten

Maps, sets, arrays, files, JSON, and errors wait for 0.13 language types. See `docs/STDLIB_SPEC.md` and `docs/STDLIB_IMPLEMENTATION_REPORT.md`.

Preserve 0.11 benchmarks (`benchmarks/results-0.11.json`). Do not overwrite them in place.

## 0.13 — Memory and systems programming

**Status: 0.13.1–0.13.6 implemented on `main`.** `v0.13.1` is local. `v0.13.6` is `eb42a0a`. See `docs/YUKT_013_DESIGN.md`.

- Records (named structs) and field access — **implemented** (`0.13.1`)
- Ownership: move of heap values, free when the owning frame returns (no GC) — **implemented** (`0.13.2`)
- `ref` borrows; no raw pointers in YUKT source — **implemented** (`0.13.3`)
- `foreign` C functions with explicit types — **implemented** (`0.13.4`)
- Error values (`ok` / `fail`) — **implemented** (`0.13.5`)
- `std/map`, `std/set`, `std/file`, `std/json` — **implemented** (`0.13.6`)

Specify syntax before implementing. This is a language milestone, not extra runtime functions behind compiler special cases.

Goal: make YUKT suitable for serious systems programming.

## 0.14 — CPU tasks

**Status: 0.14.1 released** (`v0.14.1` = `df4fda9`). macOS arm64 and Ubuntu x86-64 CI passed. See `docs/YUKT_014_DESIGN.md` and [the release](https://github.com/Ayushman88/yukt/releases/tag/v0.14.1).

- **0.14.1** — `start` / `wait`, `task[T]`, move-only args/results, host task pool, alloc mutex — **implemented**
- CUDA / GPU stay **after 0.15**, not the next slice

Apple Silicon has no CUDA. Do not claim GPU execution on a Mac.

## 0.15 — Stabilization

**Status: in progress.** Measurement gate accepted. Matmul investigation closed. See `docs/YUKT_015_DESIGN.md` and `docs/BENCHMARK_METHOD_015.md`.

0.15 does **not** add language features. It finishes the 0.14.1 foundation so later work is honest.

- Runtime correctness and CTest green on macOS arm64 and Linux x86-64
- Benchmark comparison classes as a harness contract: `sequential`, `auto_parallel`, `explicit_parallel` (do not rank across classes)
- Sequential matmul codegen parity with C / C++ / Rust is established (`results-0.15.exp1-seq-matmul.macos-arm64.json`, `publication: false`)
- Documentation that matches the compiler
- **Performance freeze:** no kernel tuning, frontend n/IV experiments, or tiled GEMM unless a real regression appears

Mutex, channels, networking, and async **moved to 0.17**. Do not implement them from the old 0.15 slice list.

## 0.16–0.18 program

**Status: authorized as one sprint, three phases.** Milestone identities stay. See `AGENTS.md`.

Do not implement a whole version in one task. Spec → plan → code → tests, one objective at a time. Do not start the next **dependent** subsystem until the current one passes. Independent work may overlap. Integration stays staged.

Finish line: a developer comfortably builds a CLI, JSON API client, HTTP server, and concurrent file processor — not a longer keyword list.

**Next objective:** write the 0.17.1 implementation plan (channels, process I/O rules, TCP, HTTP/1.1). Do not start compiler or runtime. Mutex is specified and deferred (not 0.17.1). Do not start 0.18.

## 0.16 — Practical language (phase 1)

**Status: 0.16.1–0.16.3 implemented** (macOS arm64 `ctest` 44/44; Ubuntu x86-64 CI run [41275168587](https://github.com/Ayushman88/yukt/actions/runs/41275168587) for 0.16.3). See `docs/YUKT_016_DESIGN.md` and `docs/LANGUAGE_SPEC.md` § 0.16.1, § 0.16.2, and § 0.16.3. D1–D11 frozen.

Goal: everyday programs stop fighting fixed-size lists and missing I/O.

- Dynamic collections: `list.append` / `list.remove` / `list.pop` (**implemented**)
- Standard input and environment variables (**implemented**: `std/io`, `std/env`, `yukt run` argv forward)
- Richer string operations (**implemented**: `trim` / `replace` / `lower` / `upper` / `to_int` / `to_float`)
- Ownership, memory-safety, and runtime tests for each new surface

HTTP syntax in design sketches is illustrative only and belongs to 0.17.

## 0.17 — Concurrency and networking (phase 2)

**Status: specified, not implemented.** Language law: `docs/LANGUAGE_SPEC.md` § 0.17 and `docs/STDLIB_SPEC.md` (`std/sync`, `std/net`, `std/http`). Rationale: `docs/YUKT_017_DESIGN.md` (accepted 2026-11-01). Process I/O versus `start` is implemented (0.17.1 D-IO). Next authorized coding objective is channels. Mutex is specified and deferred (not 0.17.1). GPU stays off this path.

- Process I/O versus `start` (origin-thread stdin, env lock, say lock) (**implemented**, 0.17.1 D-IO)
- Channels (`sync.channel` / `receiver` / `send` / `recv` / `close`)
- POSIX TCP (`std/net`)
- HTTP/1.1 client and server (`std/http`)
- Mutex API specified, deferred — not a 0.17.1 ship-gate
- Concurrency and networking stress tests after the 0.17.1 plan

Proof: a small HTTP server a developer can write without inventing the API in application code.

## 0.18 — Developer experience (phase 3)

**Status: proposed.** Specify before implementing.

- Interactive REPL
- Package manager, dependency and version management
- Formatter and language server
- Project creation, build, and dependency workflows
- Clean install / build / error-path validation on both hosts

## 0.19 — Portability

**Status: proposed.** Host-native macOS arm64 and Linux x86-64 already exist.

- Keep both current hosts green
- Windows target
- Cross-compilation architecture
- Target-specific runtime abstraction

## 1.0 — First usable release

**Status: proposed.** A developer can install YUKT, create a project, install dependencies, build a real application, test it, and distribute it without working around missing infrastructure.

Progress is whether someone can comfortably build these, not how many kernels YUKT wins:

| Project | What it proves |
| --- | --- |
| CLI utility | Core language and filesystem |
| JSON API client | Networking and JSON |
| HTTP server | Real application development |
| Concurrent file processor | Collections and concurrency |
| Small database | Memory, files, records, and enough performance to be usable |

Published five-language numbers remain a **regression and honesty** tool. They are not the product.

## Measurement

Publish numbers, not slogans. Keep the 0.11 baseline. Frozen files stay immutable (`benchmarks/results-0.11.json`, `docs/BENCHMARKS.md`, `benchmarks/results-0.15.macos-arm64.json`). New experiments are new files. Do not mix `sequential`, `auto_parallel`, and `explicit_parallel` in one ranking.

## Non-goals for the near term

- A Python or bytecode interpreter as a substitute for the compiler
- Syntax expansion that is not specified first
- Fake LLVM output or placeholder "compiled successfully" messages
- More benchmark-kernel wins as a milestone
- CUDA / GPU on the path to 1.0 (Apple Silicon has no CUDA)
