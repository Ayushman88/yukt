# YUKT 0.11 design — Portability and compiler intelligence

Status: implemented and frozen on top of the 0.10 compiler. This milestone does not change YUKT syntax. Audit: `docs/YUKT_011_AUDIT.md`.

## Goal

Make the compiler reliable on Apple Silicon macOS and Linux x86-64 as **host-native** builds, and make execution decisions easier to inspect. Do not cross-compile to a non-host triple. Do not add keywords, modules, structs, or CUDA.

## What 0.11 adds

1. **Host-native CI** for `macos-latest` (arm64) and `ubuntu-latest` (x86-64).
2. **Linker uses the same LLVM clang** the compiler was built against, then `xcrun` on Apple, then `PATH`.
3. **Cost model:** nested unknown inner trips count as 256 (was 64). `count` folds when the list is a literal or `zeros(n)` with a constant `n`.
4. **Simple parallel reductions** of the form `acc = acc + expr` or `acc = acc * expr` (i64/f64). Integer overflow is undefined. Float association is not stable. Allocating loop bodies stay sequential.
5. **Size-class payload reuse** in the runtime (bounded bins, main-thread only).
6. **Diagnostic notes**, a GNU `file:line:col:` first line, and TTY color (off when not a tty or `NO_COLOR`).
7. **`--explain` prints the host triple, CPU, and cost-model thresholds.** Parallel/vectorized claims still require those facts in LLVM IR.
8. **Stdlib foundations** as documentation and the allocator above. No imports, maps, JSON, or files.

## What 0.11 does not add

- Cross-compilation to a non-host triple.
- New syntax, keywords, or `use`/modules.
- Maps, sets, JSON, file I/O (those wait for stdlib APIs after the 0.12 module system).
- Ownership, structs, pointers (0.13).
- Async, thread APIs, or CUDA (0.14). CUDA is Linux+NVIDIA only when it exists; Apple Silicon has no CUDA.

## Decision pipeline

```
repeat
  → independence analysis (say / give / calls / index shape)
  → simple +/* reduction? (one i64/f64 accumulator)
  → work estimate (trip × body cost; unknown nested trip = 256)
  → nest-depth rule (inner loops need a proven trip ≥ 1e6)
  → outline + yukt_parallel_for(..., work_per_iter)   or counted sequential loop
  → reduction chunks combine with + or * after each range
  → LLVM O3
  → runtime may still sequentialize: count < 64, ncpu ≤ 1, or trip × work_per_iter < ~1e5 × ncpu
```

## Files

| Path | Role |
| --- | --- |
| `.github/workflows/ci.yml` | macOS arm64 and Ubuntu x86-64 `ctest` |
| `compiler/backend/llvm_backend.cpp` | Host clang discovery; reduction combine |
| `compiler/ir/lower.cpp` | Cost model; parallel reductions |
| `compiler/diagnostics/` | Notes, GNU line, color |
| `compiler/backend/explain.cpp` | Triple, CPU, thresholds |
| `runtime/yukt_runtime.c` | Size-class cache; reduction mutex |
| `docs/STDLIB.md` | What a later stdlib pass will need |
| `docs/EXECUTION_ENGINE.md` | Updated contract |

## Non-goals that stay non-goals

Benchmark numbers must not dictate language design. Intra-function list reuse waits for 0.13 ownership. A YUKT-written standard library waits for specified APIs after the 0.12 module system.
