# YUKT 0.09 — Performance analysis

0.08 remains the permanent compute baseline. This document records generated-code investigation, the 0.09 cost-model and memory changes, and a fairer comparison that includes sequential **and** parallel C++/Rust on the same LLVM 23 toolchain.

Nothing here is a general claim that YUKT is faster than C++ or Rust.

Host: Apple M2, arm64 Darwin. Median of 7 in-process runs (one warmup discarded). Checksums matched on every kernel.

## 0.08 baseline (kept)

| Kernel | YUKT 0.08 | Notes |
| --- | --- | --- |
| nested | 10.3 ms | `a[j]+=i`, n=8000 |
| matmul | 37.1 ms | vs sequential Apple-clang C++ ~197 ms |
| fibonacci | 36.3 ms | tied |
| sort | 38.4 ms | vs Apple clang C++ 98.3 ms |
| alloc | 24.5 ms | tied with Rust |
| parallel fill | 9.6 ms | vs explicit-thread 7.4 ms |

0.08 C++ used Apple clang 21. 0.09 C++ uses Homebrew LLVM 23.1.4, the same compiler family that lowers YUKT. That change is intentional and is **not** a YUKT speedup.

## 0.09 results

Machine-readable copies: `benchmarks/results.json`, `benchmarks/results.csv`.

| Kernel | YUKT | C++ seq | C++ par | Rust seq | Rust par | YUKT min–max | vs 0.08 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| nested | 10.3 ms | 10.5 ms | — | 10.2 ms | — | 10.2–11.1 | same |
| matmul | 49.5 ms | 198.5 ms | 55.4 ms | 199.7 ms | 49.7 ms | 42.1–75.8 | slower median, wide spread |
| fibonacci | 36.3 ms | 36.2 ms | — | 36.0 ms | — | 36.0–40.6 | same |
| sort | 45.4 ms | 39.6 ms | — | 127.3 ms | — | 38.9–77.5 | same-LLVM C++ is faster |
| alloc | 15.5 ms | 22.1 ms | — | 30.8 ms | — | 14.9–18.2 | 37% faster than 0.08 |
| parallel fill | 7.5 ms | 25.9 ms | 7.5 ms | 21.0 ms | 7.5 ms | 7.3–10.7 | tied with explicit threads |

Library algorithm, same 16000-element input as selection sort (not a YUKT feature): C++ `std::sort` 0.065 ms, Rust `sort_unstable` 0.026 ms.

## Generated-code investigation

### Nested — sequential SIMD, as intended

No `yukt_parallel_for`. Inner loop is `vector.body` with `index.next += 8` (four `<2 x i64>` operations). Tied with C++ and Rust. The 0.07 454 ms figure is not comparable (different kernel, `clang -O0` codegen).

### Matmul — auto-par, not a mysterious 5× compiler win

LLVM IR still has two range-parallel loops over `0..511` (init and compute). Inner `k` is a sequential reduction with vectorized partial sums.

0.08's 5.3× vs C++ was **automatic outer-loop parallelization vs sequential ijk**. 0.09 adds a row-parallel C++/Rust kernel using `std::thread` / `thread::scope` over `i`. YUKT 49.5 ms vs parallel Rust 49.7 ms vs parallel C++ 55.4 ms: too close to call. Sequential C++/Rust remain ~199 ms.

YUKT's matmul median is higher than 0.08 (49.5 vs 37.1) with a wide spread (42–76 ms). Min 42.1 ms is near the 0.08 band. This is treated as run-to-run noise plus extra system load from the larger suite, not as a proven codegen regression. Do not claim 0.09 matmul is faster than 0.08.

### Fibonacci — counted scalar loop

`fib` is inlined. Hot loop is three phis and `icmp eq`. Tied with C++/Rust. Recurrence cannot vectorize.

### Selection sort — LLVM 23 vectorized argmin, not a better sort

YUKT IR for the inner min scan:

- `<2 x i64>` loads, `icmp slt`, `llvm.smin.v2i64`
- index tracking with `llvm.umin`
- `llvm.vector.reduce.smin`
- assembly: `ldp q` / `cmgt.2d`

Apple clang 21 C++ (0.08 baseline) compiled the same algorithm as a scalar `ldr/cmp` loop (~100 ms). Homebrew LLVM 23 C++ vectorizes it too (~40 ms). YUKT 45.4 ms vs same-LLVM C++ 39.6 ms: C++ is faster. Rust 1.99 still emits a scalar min scan (127 ms).

The 0.08 “2.56× vs C++” does not survive a same-LLVM comparison. Selection sort is also the wrong algorithm for real sorting: `std::sort` on the same 16000 keys is 0.065 ms (~700× vs YUKT's selection sort).

### Alloc — large inner fills now parallel

`repeat p in 1..25: repeat i in 0..n-1: items[i] = i+p` with `n=4e6` has a compile-time inner trip of 4e6, which meets the nested-loop 1e6 rule. 0.08 left that sequential. 0.09 parallelizes those fills. YUKT 15.5 ms vs C++ 22.1 ms vs Rust 30.8 ms. Checksums match.

### Parallel fill — cost model now parallelizes cheap-but-huge loops

0.08 refused to parallelize any loop without a nested `repeat`, so a 20e6 fill stayed sequential (9.6 ms) while explicit-thread C++/Rust ran in 7.4 ms.

0.09 estimates work as `trip × body_cost` and parallelizes when that is ≥ 1e6, or when the trip count is unknown at compile time (runtime still sequentializes counts &lt; 64). The 20e6 fill emits `yukt_parallel_for(0, 19999999, …)` with a vectorized range thunk. YUKT 7.5 ms matches explicit-thread C++/Rust 7.5 ms and beats sequential fills (~21–26 ms).

## Changes implemented

No syntax. No benchmark-named special cases.

1. **Work-based auto-par.** `worth_parallelizing` uses constant folding of `repeat` bounds already assigned in the function. Nested loops still require a proven trip ≥ 1e6 so an inner `j` of 8000 does not dispatch 8000 times. Tests: 1e6 fill is `ParallelFor`; 16-element fill is not; `n=8` nested compute is not.

2. **Aligned lists.** `aligned_alloc(16)` + zero. LLVM `CreateAlignmentAssumption` on the data pointer. Vector loads in several kernels are `align 16`.

3. **Harness fairness.** C++ is Homebrew `clang++` 23.1.4 with `-isysroot` when needed. Sequential and parallel C++/Rust for matmul and fill. Results written to `benchmarks/results.json` and `benchmarks/results.csv`.

4. **Library sort bench** under `benches/algo/`, separate from the compiler selection-sort kernel.

## Regressions

- **Sort vs 0.08 headline:** YUKT is no longer reported as faster than C++ once C++ uses LLVM 23. That is a measurement correction, not a YUKT slowdown. Min 38.9 ms matches 0.08's 38.4 ms.
- **Matmul median 49.5 vs 0.08 37.1:** spread is large; min 42.1 ms. Not claimed as an improvement. Parallel C++/Rust show the 0.08 “5×” was parallelism, which 0.09 still has.
- **Library `std::sort` times are sub-millisecond** on n=16000. They are reported so nobody treats selection sort as a sorting-product number.

## Remaining problems

- Cost model uses compile-time constants and a single 1e6 threshold. It does not yet see cache-line sharing, NUMA, or “this fill is bandwidth-bound.”
- Matmul is still naive ijk, now parallel over `i`. A library should own blocked/tiled GEMM.
- Selection sort remains O(n²). Vectorized argmin is a compiler result, not a sort library.
- rustc 1.99 does not vectorize the min scan; that is a Rust/LLVM version gap, not a YUKT win.
- No JSON/HTTP/hash-map suite yet. See `docs/OPTIMIZATION_STRATEGY.md`.
- Linux thread pool is still unmeasured on this host.

## Verification

- `ctest --test-dir build --output-on-failure`: 13/13 passed, including a 1e6 parallel fill checksum `0` / `999999`.
- All compiler-kernel checksums matched across YUKT / C++ seq / C++ par / Rust seq / Rust par.
- Flags: YUKT 0.09, LLVM 23.1.4 `PassBuilder` O3, `llc -O3`; C++ `clang++ -O3 -march=native -std=c++17 -pthread`; Rust `opt-level=3`, `target-cpu=native`.
