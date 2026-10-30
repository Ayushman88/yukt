# YUKT 0.10 design — Intelligent Execution Engine

Status: implemented on top of the 0.09 compiler. This milestone does not change YUKT syntax.

## Goal

Make the compiler choose sequential, vectorized, or parallel execution from workload characteristics, then *show* that choice. Do not rewrite working 0.08/0.09 passes. Do not add keywords so a benchmark can opt in.

The language proposition stays the same: write simple `repeat` loops; the compiler owns execution strategy.

## What 0.10 adds

1. **Recorded loop decisions** in YUKT IR (`LoopDecision` on `IrModule`).
2. **A runtime cost check** on `yukt_parallel_for(start, end, fn, ctx, work_per_iter)` so a compile-time “maybe parallel” loop can still run sequentially when the live trip count or machine is too small.
3. **`yukt --explain file.yukt`**, which prints planned decisions and only marks parallel/vectorized as successful when those facts appear in generated LLVM IR.
4. **Payload reuse** after `yukt_runtime_release`: the largest list buffer is kept and zero-filled on the next allocation of that size or smaller.
5. **Measurement protocol**: 15 runs, median, IQR, MAD; 0.08 and 0.09 remain permanent baselines.

## What 0.10 does not add

- No new syntax, keywords, or execution-mode annotations.
- No JSON, HTTP, hashing, or file-processing language features (those need syntax and a standard library; they stay on the 1.0 roadmap).
- No YUKT-IR loop fusion, interchange, or compiler-driven unroll. Those were investigated; they are not applied because there is not yet a general legality pass that preserves every current `repeat` semantics.
- No hardcoded benchmark names or input sizes in the compiler.

## Decision pipeline

```
repeat
  → independence analysis (say / give / calls / reductions / index shape)
  → work estimate (trip × body cost)
  → nest-depth rule (inner loops need a proven trip ≥ 1e6)
  → outline + yukt_parallel_for(..., work_per_iter)   or counted sequential loop
  → LLVM O3 (vectorize, SLP, inline, DCE, unroll-if-profitable)
  → runtime may still sequentialize: count < 64, ncpu ≤ 1, or trip × work_per_iter < ~1e5 × ncpu
```

`--explain` reports the compile-time row of that pipeline and verifies the LLVM IR row. It does not claim a runtime sequentialization that only happens when the program runs.

## Cost model

Unchanged compile-time threshold from 0.09, plus a runtime tax:

| Input | Role |
| --- | --- |
| Estimated trip count | Inclusive `start..end` when both bounds fold |
| Body cost | Assignments + 2× index assigns + nested trips (unknown inner trip counts as 64) |
| Nest depth | Nested `repeat` parallelizes only with a proven trip ≥ 1_000_000 |
| Memory access | List writes must mention the iterator; reductions stay sequential |
| CPU count | `sysconf(_SC_NPROCESSORS_ONLN)`, capped at 32 |
| Scheduling overhead | Runtime sequentializes when `count * work_per_iter < 100000 * ncpu` |

Vectorization is still LLVM’s decision. YUKT prepares counted loops, NSW arithmetic, 16-byte aligned payloads, and `llvm.assume` alignment. `--explain` reports SIMD only when `vector.body` or `llvm.vector.reduce` is in the generated IR.

## Investigated compiler transforms

| Transform | 0.10 action |
| --- | --- |
| Loop fusion | Investigated. Not applied: adjacent `repeat`s often have intervening scalars or different bounds; no general dependence test yet. |
| Loop interchange | Investigated. Not applied: YUKT `repeat` is inclusive and may carry reductions on an inner index; legality is not proven in IR. |
| Loop unrolling | Left to LLVM O3. |
| SIMD vectorization | LLVM loop vectorize / SLP after counted lowering. |
| Reduction optimization | Sequential LLVM vectorization only. No parallel reduction yet. |
| Function inlining | `InlineHint` on YUKT functions; `AlwaysInline` on outlined parallel bodies. LLVM O3 inlines. |
| Dead code elimination | LLVM O3. |
| Memory reuse | Runtime payload cache after `yukt_main` returns. Not a store-forwarding analysis. |

## Files

| Path | Role |
| --- | --- |
| `compiler/ir/ir.hpp` | `LoopDecision`, latch `loop_iterator`, `IrInst.work` |
| `compiler/ir/lower.cpp` | Records decisions; passes work into `ParallelFor` |
| `compiler/backend/explain.cpp` | `--explain` text, verified against LLVM IR |
| `compiler/backend/llvm_backend.cpp` | `yukt.iterator` loop metadata; 5-argument `yukt_parallel_for` |
| `runtime/yukt_runtime.c` | Adaptive parallel + payload cache |
| `docs/EXECUTION_ENGINE.md` | How decisions are made |
| `docs/PERFORMANCE_ANALYSIS_0.10.md` | Measured numbers |

## Non-goals that stay non-goals

Benchmark numbers must not dictate language design. If a kernel is slow, the next step is a general analysis or a library algorithm, not a special case for that file.
