# YUKT benchmarks

These numbers are **in-process kernel time**, not process startup. Each binary discards one warmup call, then times the second call with a monotonic clock (`compute_ns` on stderr). The table is the median of 15 process runs.

Host tables are **not interchangeable**. The table below is Apple M2, arm64 Darwin. Publish Linux x86-64 as a separate section after measuring on that machine. `benchmarks/results.json` records `os`, `arch`, `cpu`, `llvm_version`, and `clang_path`.

## Compute

| Kernel | Size | YUKT | C++ seq | C++ par | Rust seq | Rust par | YUKT IQR | YUKT MAD | YUKT min–max | Checksum |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| nested | n=8000 nested a[j]+=i plus checksum | 0.0102s | 0.0102s | — | 0.0102s | — | 0.0000s | 0.0000s | 0.0102s–0.0102s | yes |
| matmul | 512x512 integer matmul, naive ijk | 0.0374s | 0.1955s | 0.0502s | 0.1972s | 0.0650s | 0.0015s | 0.0008s | 0.0355s–0.0397s | yes |
| fibonacci | 1_500_000 calls of iterative fib(45) (opaque arg) | 0.0361s | 0.0361s | — | 0.0361s | — | 0.0002s | 0.0001s | 0.0354s–0.0366s | yes |
| sort | selection sort, n=16000 | 0.0383s | 0.0392s | — | 0.1223s | — | 0.0001s | 0.0000s | 0.0382s–0.0383s | yes |
| alloc | zeros + 25 fills, n=4_000_000 | 0.0130s | 0.0243s | — | 0.0193s | — | 0.0069s | 0.0003s | 0.0127s–0.0396s | yes |
| parallel | independent fill + sequential reduction, n=20_000_000 | 0.0075s | 0.0258s | 0.0076s | 0.0307s | 0.0074s | 0.0002s | 0.0001s | 0.0072s–0.0083s | yes |

## Compile time and binary size (`matmul`)

| Compiler | Compile time | Binary size |
| --- | --- | --- |
| YUKT --emit-llvm | 0.2194s | 38264 bytes |
| C++ | 1.1761s | 36312 bytes |
| Rust | 0.6505s | 454104 bytes |

## What this run supports

- Speed claim this run supports: matmul (512x512 integer matmul, naive ijk): YUKT 0.0374s vs fastest other 0.0502s. alloc (zeros + 25 fills, n=4_000_000): YUKT 0.0130s vs fastest other 0.0193s. That is not a general claim that YUKT is faster than C++ or Rust.
- nested: too close to call — Rust seq 0.0102s vs C++ seq 0.0102s. Full order: Rust seq 0.0102s, C++ seq 0.0102s, YUKT 0.0102s.
- matmul: YUKT 0.0374s vs C++ par 0.0502s (1.34×). Full order: YUKT 0.0374s, C++ par 0.0502s, Rust par 0.0650s, C++ seq 0.1955s, Rust seq 0.1972s.
- fibonacci: too close to call — C++ seq 0.0361s vs YUKT 0.0361s. Full order: C++ seq 0.0361s, YUKT 0.0361s, Rust seq 0.0361s.
- sort: too close to call — YUKT 0.0383s vs C++ seq 0.0392s. Full order: YUKT 0.0383s, C++ seq 0.0392s, Rust seq 0.1223s.
- alloc: YUKT 0.0130s vs Rust seq 0.0193s (1.48×). Full order: YUKT 0.0130s, Rust seq 0.0193s, C++ seq 0.0243s.
- parallel: too close to call — Rust par 0.0074s vs YUKT 0.0075s. Full order: Rust par 0.0074s, YUKT 0.0075s, C++ par 0.0076s, C++ seq 0.0258s, Rust seq 0.0307s.

## Method

- Host: arm64 Darwin (Apple M2) — do not replace with a Linux run
- YUKT yukt 0.11 / LLVM IR codegen with /opt/homebrew/opt/llvm/bin/llc -O3, linked with the same LLVM clang the compiler was built against
- C++: /opt/homebrew/opt/llvm/bin/clang++ -O3 -march=native (same LLVM family as YUKT); Rust: rustc 1.99.1 (58cf8f9c3 2026-10-13) (Homebrew)
- YUKT timing wraps `yukt_main` (CRT `main` stripped). C++/Rust time the kernel function with `steady_clock` / `Instant`.
- One warmup call is discarded in-process, then the second call is timed. Median, IQR, and MAD of 15 process runs.
- C++ is Homebrew LLVM clang++ when present (same LLVM as YUKT), not Apple clang.
- C++/Rust `matmul` report sequential ijk and a row-parallel variant. YUKT auto-parallelizes when estimated work >= 1e6.
- C++/Rust `parallel` report sequential and explicit-thread fills. YUKT may auto-parallelize a large independent fill.
- A language is only called faster on a kernel when its median is at least 5% below the next one.
- 0.08 and 0.09 YUKT medians are kept as permanent baselines in docs/PERFORMANCE_ANALYSIS_0.08.md and docs/PERFORMANCE_ANALYSIS_0.09.md.
- Re-run with `python3 benches/run.py` after `cmake --build build`.
