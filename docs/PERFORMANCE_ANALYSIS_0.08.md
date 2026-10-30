# YUKT 0.08 — Performance analysis

This document is the 0.08 performance-engineering record. It keeps the published 0.07 compute baseline, states the measured cause of each gap, and reports 0.08 numbers from the same harness protocol (in-process warmup discarded, monotonic `compute_ns`, median of 7 runs, checksums must match).

Nothing here is a general claim that YUKT is faster than C++ or Rust.

Host for both tables: Apple M2, arm64 Darwin. C++: `c++ -O3 -march=native`. Rust: rustc 1.99.1, `opt-level=3`, `target-cpu=native`.

## 0.07 baseline (kept)

These are the published 0.07 medians. Nested was `n=16000` product-sum. YUKT IR was codegen'd with `clang -c -x ir -O0`.

| Kernel | YUKT | Fastest baseline | Gap |
| --- | --- | --- | --- |
| nested | 454.2 ms | Rust 96.4 ms | 4.71× |
| matmul | 117.1 ms | sequential C++/Rust ~195 ms | YUKT win (auto-par vs sequential) |
| fibonacci | 155.1 ms | C++ 35.8 ms | 4.33× |
| sort | 130.0 ms | C++ 97.3 ms | 1.34× |
| alloc | 50.1 ms | Rust 19.1 ms | 2.62× |
| parallel fill | 35.1 ms | C++/Rust 7.4 ms | 4.72× |

## 0.08 results

Nested is a different kernel than 0.07: `n=8000` `items[j] += i` plus a checksum. The old product-sum closed-formed after counted-loop + NSW lowering (see nested root cause). Compare nested to C++/Rust on the new kernel, not to the 0.07 454 ms row.

YUKT IR is now codegen'd with `llc -O3` (the same LLVM codegen `yukt build` uses). `clang -O0` on already-optimized IR was penalizing YUKT in 0.07.

| Kernel | YUKT median | C++ | Rust | YUKT min–max | Checksum |
| --- | --- | --- | --- | --- | --- |
| nested (`n=8000` `a[j]+=i`) | 10.3 ms | 10.3 ms | 10.3 ms | 10.2–12.4 ms | yes |
| matmul 512×512 ijk | 37.1 ms | 196.9 ms | 196.2 ms | 35.8–40.1 ms | yes |
| fibonacci 1.5e6×fib(45) | 36.3 ms | 36.4 ms | 36.2 ms | 36.2–36.4 ms | yes |
| selection sort n=16000 | 38.4 ms | 98.3 ms | 123.5 ms | 38.4–38.5 ms | yes |
| alloc zeros+25 fills, n=4e6 | 24.5 ms | 24.8 ms | 24.5 ms | 20.0–27.8 ms | yes |
| parallel fill n=20e6 | 9.6 ms | 7.6 ms | 7.4 ms | 9.5–12.0 ms | yes |

Speed claims this run supports (median at least 5% below both baselines, kernel ≥ 1 ms):

- matmul: YUKT 37.1 ms vs sequential C++ 196.9 ms and Rust 196.2 ms (about 5.3×). C++/Rust stay sequential naive ijk. YUKT auto-parallelizes the independent outer `repeat`.
- sort: YUKT 38.4 ms vs C++ 98.3 ms and Rust 123.5 ms. Same selection-sort algorithm; the compiler did not switch algorithms.

nested, fibonacci, and alloc are too close to call. parallel fill is still slower than explicit-thread C++/Rust (1.29×).

## Phase 1 — Root cause of each 0.07 gap

Diagnosed from 0.07 LLVM IR, assembly, and the runtime, not guessed.

### Nested loops (454 ms vs 96 ms)

Inclusive `repeat i in start..end` lowered as `i <= end`. LLVM built an i128 overflow-safe trip count instead of a canonical `k < niter` counted loop. The inner product-sum *did* vectorize (VF=2) and strength-reduce multiplies, but the extra IV and trip-count checks still lost about 4.7× to Rust.

After counted-loop + NSW lowering, the *original* `Σ i*j` kernel with a runtime `n` still became a closed-form polynomial in `n` (LLVM IR was a handful of muls, no loop). That is a real compiler win and a dead benchmark. The 0.08 nested kernel writes `items[j] += i` so the loops actually run.

A second 0.07 penalty: `clang -c -x ir -O0` codegen of vector IR. With `llc -O3`, the memory nested kernel ties C++ and Rust at 10.3 ms.

### Fibonacci (155 ms vs 36 ms)

`fib` was already inlined at O3. The loop used `icmp sgt i+1, n` plus a live iterator, not a counted `k < niter`. Combined with `clang -O0` codegen of that scalar loop, YUKT was 4.3× slower.

0.08 counted lowering produces:

```
%5 = add nsw i64 %a, %b
%6 = add nuw nsw i64 %k, 1
%exitcond = icmp eq i64 %k, %n-2
```

`fib` is inlined into `yukt_main`. With `llc -O3`, YUKT 36.3 ms vs Rust 36.2 ms.

### Matrix multiplication (117 ms vs ~195 ms sequential)

Not a bug. YUKT auto-parallelized the outer `i` loop; C++/Rust benches stay sequential ijk. 0.07's remaining cost was per-index `yukt_parallel_for` callbacks into C (one function call per `i`, and init fills also paid dispatch overhead).

### Selection sort (130 ms vs 97 ms)

The algorithm is quadratic and not vectorized as a sort. 0.07 extra cost was the same counted-loop / O0-codegen tax as nested and fib. 0.08 does not change the sort. LLVM O3 on counted loops plus `llc -O3` made YUKT's min-scan faster than the C++ `std::vector` version on this host. Checksums match.

### Allocation (50 ms vs 19 ms)

Two issues:

1. 25 fills of 4e6 elements each went through per-index parallel callbacks (nested inside `repeat p`). Dispatch overhead dominated.
2. Lists were never freed. The harness calls `yukt_main` twice in one process; the timed call `calloc`'d a fresh region and paid page faults. C++ `vector` reuse after destructor is cheaper.

### Parallel fill (35 ms vs 7.4 ms)

The thunk was `void(i64 index, ptr ctx)` and the runtime invoked it once per element (20e6 C calls). That, not missing SIMD, was the 4.7× hole. After switching to range callbacks, auto-par of a *cheap* fill was still a pessimization: chunked runtime trip counts vectorized worse than a constant-trip sequential fill, and the leaked `zeros(20e6)` on the timed call still paid a 160 MB `calloc`.

## Phase 2 and 3 — Changes implemented

No syntax was added. No benchmark-named special cases.

### Loop lowering

- Inclusive `repeat` now computes `niter = end - start + 1` and iterates `k < niter` with a separate iterator starting at `start`. Empty when `end < start`.
- Integer `+ - *` lower as NSW.
- Loop latches get `!llvm.loop.mustprogress`. Forced `vectorize.enable` was tried and removed: it warned on fib's recurrence and did not help.

### Inlining and list access

- Non-main functions get `inlinehint`; outlined `$par$*` functions get `alwaysinline`.
- `yukt_list_zeros` / `yukt_list_new` return `noalias`. List data loads are `!invariant.load` + `!noundef`.
- Runtime declarations are `nounwind`.

### Automatic parallelization cost model

Independence rules are unchanged (no `say`/`give`/user calls, no reductions, list writes mention the iterator). Added:

- Do not parallelize a loop whose body has no nested `repeat`. A fill of `items[i] = ...` stays sequential and vectorizes (nested inner step 8, parallel fill step 4).
- A loop nested inside another `repeat` only parallelizes when its trip count is a compile-time constant ≥ 1_000_000.
- Runtime sequential cutoff is 64 iterations so a 512-row matmul still parallelizes.

matmul's outer `i` contains nested `j`/`k` and still goes through `yukt_parallel_for(0, 511, ...)`. The parallel-fill bench does **not** call `yukt_parallel_for`.

### Parallel runtime

- Callback type is `void(int64_t begin, int64_t end, void* ctx)` (inclusive range). The loop lives in LLVM, not in C.
- One chunk per CPU. macOS uses `dispatch_apply`. Linux uses a persistent pthread pool (no `pthread_create` per loop).
- Nested auto-par inside an already parallel region remains forbidden (`in_parallel_`).

### Memory

- `yukt_runtime_release` frees tracked lists and concatenated strings.
- LLVM inserts that call on every return from `yukt_main`, so a second in-process `yukt_main` can reuse heap pages.

### Harness

- YUKT kernel objects: `llc -O3 -filetype=obj` on `--emit-llvm` output with CRT `@main` renamed. This matches `yukt build` codegen and avoids Apple clang rejecting LLVM 23 IR attributes.
- Nested C++/Rust/YUKT kernels updated together to `a[j] += i`.

## Generated code (0.08)

Nested inner loop (sequential, vectorized, 8 i64s per iteration): `vector.body` with `<2 x i64>` loads/adds/stores, `index.next += 8`. No `yukt_parallel_for`.

matmul: two range parallel loops over `0..511` (init and compute). Inner `k` stays a sequential reduction.

fibonacci: `fib` inlined; hot loop is three phis (`a`, `b`, `k`) and `icmp eq`.

parallel fill: no `yukt_parallel_for`; fill and reduction are both `vector.body` with `index.next += 4`.

## Before / after (YUKT median)

| Kernel | 0.07 YUKT | 0.08 YUKT | vs fastest 0.08 baseline |
| --- | --- | --- | --- |
| nested | 454.2 ms (product-sum n=16000) | 10.3 ms (a[j]+=i n=8000) | tied |
| matmul | 117.1 ms | 37.1 ms | 5.3× faster than sequential C++/Rust |
| fibonacci | 155.1 ms | 36.3 ms | tied |
| sort | 130.0 ms | 38.4 ms | 2.56× faster than C++ |
| alloc | 50.1 ms | 24.5 ms | tied with Rust |
| parallel | 35.1 ms | 9.6 ms | 1.29× slower than Rust 7.4 ms |

Compile (`matmul` `--emit-llvm`): YUKT 0.21 s / 37736 bytes. C++ 0.88 s / 36312 bytes. Rust 0.99 s / 454104 bytes.

## Regressions

No 0.07 kernel got slower on the same workload except where the nested *source* changed. The old product-sum is no longer timed because it is no longer a loop.

parallel fill is still 1.29× behind explicit threads. That is a remaining gap, not a regression vs 0.07 (35.1 ms → 9.6 ms).

## Remaining problems

- **Explicit vs automatic parallel.** Cheap fills are sequential by design. Explicit-thread C++/Rust still win the 20e6 fill (7.4 ms vs 9.6 ms). A later Sequential / Automatic / Parallel execution mode would make that choice visible in the language; 0.08 did not add syntax.
- **No parallel C++/Rust matmul.** The 5.3× matmul claim is against sequential ijk, not against a blocked or threaded C++/Rust matmul.
- **Selection sort is still O(n²).** The 2.56× win is codegen, not a better algorithm. A later standard library should provide a real sort.
- **No cross-compilation, no stdlib.** Unchanged.
- **Linux thread pool is unmeasured here.** This run is macOS libdispatch.
- **Bounds checks** remain off for i64/f64 GEP (0.07 SIMD decision). Safety is still "programmer index is in range".

## Verification

After the 0.08 changes:

1. `ctest --test-dir build --output-on-failure` — 13/13 passed (empty `5..1` loops, nested `a[j]+=i` checksum 224, parallel nested-body checksum 8192, existing native programs).
2. All six bench checksums matched across YUKT / C++ / Rust.
3. All three implementations rebuilt by `python3 benches/run.py`.
4. Medians and min–max recorded in `docs/BENCHMARKS.md`.

Compiler flags for YUKT: LLVM 23.1.4, `PassBuilder` O3, host CPU, NSW integer arith, `llc -O3` codegen. C++/Rust flags as above.
