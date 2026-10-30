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

Verified on Apple Silicon macOS (`arm64`). Linux uses the same CMake/LLVM path; it is not yet CI-tested.

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
- Reductions and loops with `say` / `give` / user calls stay sequential

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

## 0.11 — Standard library

- Core collections, I/O, and numeric helpers written in YUKT where possible
- Runtime support only where the language cannot express it
- Optimized sort / matmul / reduction as library algorithms, not compiler special cases

## Later — First usable release

- Documented language
- Native binaries for supported targets
- Tested standard library
- Published benchmarks: nested loops, matrix multiplication, Fibonacci, sorting, allocation, parallel workloads, compile time, and binary size

## Measurement

Benchmarks can begin now that native binaries exist. Publish numbers, not slogans.

## Non-goals for the near term

- A Python or bytecode interpreter as a substitute for the compiler
- Syntax expansion that is not specified first
- Fake LLVM output or placeholder "compiled successfully" messages
