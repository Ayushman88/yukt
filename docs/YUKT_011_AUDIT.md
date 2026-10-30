# YUKT 0.11 freeze audit

Audit of the four freeze areas. 0.12 is not started.

## 1. Parallel reductions — integer overflow and float order

**Integer overflow:** LLVM lowering uses `nsw` add/mul for i64. Signed overflow is **undefined** in both sequential and parallel code. `yukt_reduce_i64` uses C `+=` / `*=` on `int64_t`, which is also undefined on overflow. YUKT does not define wraparound.

**Integer `+`/`*` without overflow:** associative. Chunk partials plus a mutex combine match a sequential fold.

**Float `+`/`*`:** not associative. Parallel chunking (one partial per CPU, then combine) may produce **different rounding results** than a left-to-right sequential fold, and may differ when chunking or CPU count changes. LLVM vectorization of a sequential float loop may also change association and rounding. This is specified in `docs/LANGUAGE_SPEC.md` (Numerical semantics). It is not a bug.

**Tests:** `tests/ir/ir_tests.cpp` (1e6 integer reduction outlines); `tests/backend/native_tests.cpp` (sum of 1e6 ones = 1000000; sum of 1e6 `1.0` values prints `1000000` because 1e6 is exact in f64).

## 2. Memory allocator — bounds, threads, retention

**Bounds:** six size classes (16 … 1 MiB), four slots each, separate list and string caches. Put fails (and `free`s) when the bin is full or the block is larger than 1 MiB. Allocations are rounded up to bin capacity so a cached 16-byte slot is never handed to a 64-byte request.

**Thread safety:** caches and `yukt_allocs` are not locked. 0.11 **refuses to parallelize** a `repeat` whose body allocates (`zeros`, list literals, string concat). Parallel fills only write existing payloads. Parallel reductions combine through `yukt_reduce_mu`.

**Retention:** after `yukt_main` returns, at most four slots per class remain in each of the two caches. Maximum retained payload memory is **8,948,352 bytes**. See `docs/RUNTIME.md`. That is the warmup-reuse path for the bench harness. Not a GC.

**Tests:** native reuse (`zeros` after `yukt_runtime_release` is zeroed); IR: 1e6-trip `tmp = zeros(1)` stays sequential with reason `allocates`.

## 3. Cost model — cheap work stays sequential

Compile-time: outline only if trip is unknown **or** `trip × body cost ≥ 1_000_000`. Nested loops need a proven trip ≥ 1e6. Unknown nested inner trips count as 256 (not 64).

Runtime: sequential if trip `< 64`, `ncpu ≤ 1`, or `trip × work_per_iter < 100000 × ncpu`.

**Tests:** 16-element fill and reduction are sequential; 100-element fill is sequential; 1e6 fill is parallel. Nested `n=8` is sequential.

Unknown trip counts still outline; the runtime sequentializer is the backstop for small live `n`.

## 4. Portability — CI

Workflow: `.github/workflows/ci.yml` (`macos-latest` arm64, `ubuntu-latest` x86-64), `ctest --output-on-failure`.

Host LLVM APIs differ: LLVM 23 (Homebrew) takes `Triple`; Ubuntu’s LLVM 18 takes string/`StringRef` for `lookupTarget` / `createTargetMachine` / `setTargetTriple`, and `getHostCPUFeatures` writes an out-parameter. `compiler/backend/llvm_backend.cpp` gates those on `LLVM_VERSION_MAJOR`.

**Local (Apple Silicon macOS, LLVM 23):** `ctest --test-dir build --output-on-failure` — **14/14 passed**.

**GitHub Actions** on `main` at `80e0411` — run [40998206191](https://github.com/Ayushman88/yukt/actions/runs/40998206191) **success**:

| Job | Runner | Result |
| --- | --- | --- |
| macOS arm64 | `macos-latest` | 14/14 passed (37s) |
| Ubuntu x86-64 | `ubuntu-latest` | 14/14 passed (57s) |

The previous run on `be4be29` (`40997972412`) failed Linux at `configure_target` against LLVM 18; that is fixed.

## Freeze rule

**YUKT 0.11 is frozen** (tag `v0.11.0`). The 0.12 module system is specified in `docs/LANGUAGE_SPEC.md`; do not implement it in the compiler until that design is accepted.
