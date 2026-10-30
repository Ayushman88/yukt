# YUKT 0.10 — Performance analysis

0.08 and 0.09 remain permanent compute baselines. This document records the Intelligent Execution Engine, generated-code checks (including allocation work), and a 15-run comparison against sequential and parallel C++/Rust on LLVM 23.1.4.

Host: Apple M2, arm64 Darwin. Harness: in-process warmup discarded, second call timed, median / IQR / MAD of 15 runs, checksums must match. C++ is Homebrew `clang++` 23.1.4, the same LLVM family that lowers YUKT.

## Permanent baselines (kept)

| Kernel | YUKT 0.08 | YUKT 0.09 | YUKT 0.10 |
| --- | --- | --- | --- |
| nested | 10.3 ms | 10.3 ms | 10.2 ms |
| matmul | 37.1 ms | 49.5 ms | 37.4 ms |
| fibonacci | 36.3 ms | 36.3 ms | 36.1 ms |
| sort | 38.4 ms | 45.4 ms | 38.3 ms |
| alloc | 24.5 ms | 15.5 ms | 13.0 ms |
| parallel fill | 9.6 ms | 7.5 ms | 7.5 ms |

0.09 C++ used Homebrew LLVM 23, not Apple clang. That correction stays.

## 0.10 results

Median runtime in milliseconds. Lower is better. Parallel columns are omitted when the suite has no equivalent threaded kernel.

| Kernel | YUKT | C++ seq | C++ par | Rust seq | Rust par | YUKT IQR | YUKT MAD | min–max | Checksum |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| nested | 10.2 | 10.2 | — | 10.2 | — | 0.016 | 0.007 | 10.2–10.2 | yes |
| matmul | 37.4 | 195.5 | 50.2 | 197.2 | 65.0 | 1.5 | 0.75 | 35.5–39.7 | yes |
| fibonacci | 36.1 | 36.1 | — | 36.1 | — | 0.16 | 0.085 | 35.4–36.6 | yes |
| sort | 38.3 | 39.2 | — | 122.3 | — | 0.071 | 0.039 | 38.2–38.3 | yes |
| alloc | 13.0 | 24.3 | — | 19.3 | — | 6.9 | 0.32 | 12.7–39.6 | yes |
| parallel fill | 7.5 | 25.8 | 7.6 | 30.7 | 7.4 | 0.23 | 0.13 | 7.2–8.3 | yes |

Correctness: 6/6 checksums match.

Library sort on the same 16000 keys (not a YUKT language feature): `std::sort` 0.055 ms, Rust `sort_unstable` 0.022 ms.

## Major findings

### Nested loops — tied

YUKT 10.2 ms, C++ 10.2 ms, Rust 10.2 ms. MAD 0.007 ms. `--explain` reports both repeats sequential (outer index is `j`, not `i`; inner trip is not a proven 1e6). LLVM IR has `vector.body` with `index.next += 8` and no `yukt_parallel_for`. Same shape as 0.09.

An earlier 0.10 attempt on a noisier machine showed YUKT nested at 15.9 ms (min 11.4, max 44.5). IR was still vectorized. 15 runs plus IQR/MAD are what catch that; the quiet rerun matches 0.08/0.09.

### Matrix multiplication — competitive, not a confirmed win over parallel C++

YUKT 37.4 ms (IQR 1.5, MAD 0.75, min 35.5). Parallel C++ 50.2 ms but min 36.1 and IQR 13.9 — that range overlaps YUKT. Parallel Rust 65.0 ms, min 40.1, IQR 15.1. Sequential C++/Rust remain ~196 ms.

`--explain` verifies two `yukt_parallel_for` calls (init fill and the `i` loop of ijk). Inner `k` stays sequential (reduction, already inside a parallel region). LLVM still emits `vector.body` in the thunk.

This run’s median is 1.34× below parallel C++. Do **not** treat that as statistically established: parallel C++’s min sits inside YUKT’s band, and 0.09 was a tie with YUKT 49.5 / Rust-par 49.7 / C++-par 55.4 and a 42–76 ms YUKT spread. 0.10’s YUKT matmul is tighter (35.5–39.7). Stability improved; a headline win did not.

### Fibonacci — tied

36.1 ms all around. No parallel plan. NSW arithmetic observed.

### Sorting — essentially tied with same-LLVM C++

YUKT 38.3 vs C++ 39.2 vs Rust 122.3. Still selection sort. `std::sort` on the same input is ~700× faster. The compiler must not grow a special case for this kernel.

### Allocation — measured advantage, mixed causes

YUKT 13.0 ms vs sequential Rust 19.3 vs sequential C++ 24.3. Checksums match.

Generated LLVM IR contains **25** `yukt_parallel_for(0, 3999999, …, i64 3)` calls after `zeros`. That is the same 25 × 4e6 stores as C++/Rust; YUKT parallelizes each fill because the inner trip is a proven 4e6. C++ and Rust alloc benches stay sequential. Outer `p` stays sequential (`items[i]` is not indexed by `p`).

The timed call is the second `yukt_main` in-process. `yukt_runtime_release` after the warmup keeps the largest payload; the timed `zeros` can reuse that buffer and `memset` it. C++/Rust allocate a new `vector` / `Vec` on every `kernel()` call.

So the 1.48× vs sequential Rust is real on this harness, and it is **not** “YUKT emits a faster sequential fill loop.” It is automatic parallel fills plus a one-slot payload cache, on identical store counts. A sequential C++ fill with a reused buffer is not in the suite.

### Parallel fill — tied with explicit threads

YUKT 7.5 ms, C++ threads 7.6 ms, Rust threads 7.4 ms. Sequential fills 25.8 / 30.7 ms.

`--explain`: fill `planned: parallel`, `verified parallel: yes`, thunk `vector.body`. Reduction `planned: sequential` because `total` is used after the loop; `verified vectorized: yes` (`llvm.vector.reduce` in `yukt_main`).

That is the language-design result 0.09 established and 0.10 still holds: simple `repeat`, no thread API, matching explicit-thread C++/Rust on this workload.

## 0.10 compiler changes that can affect time

- `yukt_parallel_for(..., work_per_iter)`. Runtime sequentializes when `count < 64`, `ncpu ≤ 1`, or `count * work_per_iter < 100000 * ncpu`. Matmul’s 512 × 526337 work stays parallel. A 20e6 fill stays parallel. Tiny loops stay sequential.
- One-slot payload cache after `yukt_runtime_release`.
- `yukt.iterator` loop metadata for `--explain`. LLVM O3 is still `PassBuilder` default O3.

No benchmark name or input size is special-cased.

## Execution explanation (verified)

`yukt --explain examples/hello.yukt`: `repeat i in 1..5` is sequential because the body contains `say`. No `yukt_parallel_for`.

`yukt --explain benches/yukt/parallel.yukt`: fill verified parallel; reduction verified sequential and vectorized.

`--explain` does not report fusion, interchange, or YUKT-level unroll as successful. Those were investigated and not applied as YUKT-IR passes.

## What 0.10 is allowed to claim

- Automatic parallelization remains useful: parallel fill still matches explicit-thread C++/Rust.
- Adaptive sequentialization did not sequentialize matmul or the 20e6 fill on this 8-core host.
- `--explain` is honest about hello, fills, reductions, and verified LLVM facts.
- Alloc is faster than the sequential C++/Rust kernels in this suite; the causes are parallel fills and payload reuse, not missing stores.

## What 0.10 must not claim

- Broad superiority over C++ or Rust.
- A statistically established matmul win against parallel C++ (overlapping mins; noisy parallel baselines).
- That fusion, interchange, or compiler-driven unroll fired.
- JSON / HTTP / hashmap performance (those programs cannot be written in 0.10 syntax).
- That nested, fib, or sort got a new 0.10 algorithm. They did not.

## Method notes

- 15 process runs, median / IQR / MAD. One noisy nested suite (median 15.9 ms) was not published as a codegen regression.
- Same algorithm, inputs, and checksum in YUKT / C++ / Rust.
- Speed-claim rule in `benches/run.py` now ranks sequential **and** parallel peers. A 5% median gap against sequential ijk is no longer reported as a matmul win.
- Re-run with `python3 benches/run.py` after `cmake --build build`.
