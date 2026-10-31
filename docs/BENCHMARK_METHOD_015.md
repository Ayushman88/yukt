# YUKT 0.15 benchmark method

Status: **accepted (2026-11-01).** Implementation may extend `benches/`. Do not overwrite `benchmarks/results-0.11.json`.

This is the method for YUKT’s first independent performance evaluation after `v0.14.1`. The goal is **reproducible, defensible claims**, not attractive numbers. A run that cannot be repeated on the named host with the recorded compilers is not a result.

`docs/BENCHMARKS.md` and `benchmarks/results-0.11.json` remain the **0.11 freeze**. This method does not overwrite them.

## 1. Languages

Every kernel that is used for a claim must exist in all five languages, except where a row is marked **YUKT-only** (usability program) or **class-labeled** (task pools; see §9).

| Language | Role | Implementation constraint |
| --- | --- | --- |
| **YUKT** | Subject | Host-native `yukt` 0.14.1+; same algorithm as the others |
| **C** | Baseline | ISO **C11** (`-std=c11`). Same `clang` family used to link YUKT programs. Not Apple clang when Homebrew LLVM is the YUKT linker. This is not C17 and not C++ |
| **C++** | Baseline | ISO **C++17** (`-std=c++17`). Same `clang++` as C when possible |
| **Rust** | Baseline | `rustc`, `opt-level=3` |
| **Go** | Baseline | `gc` toolchain. Default `go build` optimizations. Not gccgo unless a later revision says so |

Do not add a sixth language in the first 0.15 suite. Do not drop C because C++ exists: C is the closest ABI neighbor to `libyukt_runtime.a`.

## 2. Platforms

Two **publication hosts**. Tables are not interchangeable.

| Host id | OS | Arch | Role |
| --- | --- | --- | --- |
| `macos-arm64` | Darwin | arm64 | Required. Record CPU brand (`sysctl machdep.cpu.brand_string`) |
| `linux-x86-64` | Ubuntu | x86-64 | Required. Record CPU model (`/proc/cpuinfo` model name) and `nproc` |

A dedicated Linux VPS may be the `linux-x86-64` publication host if its CPU, kernel, and compiler versions are recorded. It does **not** replace Ubuntu x86-64 CI (§10). GitHub-hosted runners may run the suite for checksums; they are **not** publication hosts for “faster than” claims (noisy neighbors, variable CPU).

Do not average macOS and Linux into one number. Do not replace the 0.11 Apple M2 table with a Linux run.

## 3. Compiler versions and flags

Record the **exact** strings from the binaries that produced the objects, not “LLVM 18+”.

Required fields in every `results-0.15.json` host object:

| Field | Source |
| --- | --- |
| `yukt_version` | `yukt --version` |
| `llvm_version` | `clang --version` first line (the clang that links YUKT) |
| `clang_path` | Absolute path |
| `c_compiler` | `clang --version` + flags |
| `cxx_compiler` | `clang++ --version` + flags |
| `rustc_version` | `rustc --version` |
| `go_version` | `go version` |
| `os`, `arch`, `cpu`, `ncpu` | Host |
| `compiler_flags` | Map of language → flag string actually passed |

Locked flags for the first 0.15 suite (implementation may add sysroot / `-pthread` on Linux; those must appear in the recorded string):

| Language | Flags |
| --- | --- |
| YUKT kernel object | `yukt --emit-llvm` then the same LLVM `llc -O3` that sits next to the link `clang`; link `-O3` with `libyukt_runtime.a` |
| YUKT product compile (secondary) | `yukt build` as a user would; this time is **not** the kernel `compute_ns` |
| C | `-O3 -march=native -std=c11` (`-pthread` when the kernel uses threads) |
| C++ | `-O3 -march=native -std=c++17 -pthread` |
| Rust | `-C opt-level=3 -C target-cpu=native` |
| Go | `go build` with default `gc` optimizations. Record `GOMAXPROCS` (default = `ncpu`). Do not use `-race` |

If Homebrew LLVM clang is the YUKT linker on macOS, C and C++ must use that clang, not `/usr/bin/clang`. Mixing Apple clang and LLVM 23 is an invalid run.

Do not enable LTO, PGO, or language-specific “fast math” in the first suite. If a later revision adds them, every language gets the same class of switch or the row is dropped.

## 4. Workloads

Identical **algorithm**, **input size**, and **expected stdout**. Integer kernels only for the first suite (no float association fights). Sizes stay large enough that a median under 1 ms is treated as a fold, not a win (§5).

### 4.1 Compute kernels (keep 0.11 meaning)

| Kernel | Size / algorithm | Expected stdout |
| --- | --- | --- |
| `nested` | `n=8000`, `a[j] += i`, then checksum | The same integer all five languages print |
| `matmul` | 512×512 naive ijk integer matmul + checksum | Same integer |
| `fibonacci` | 1_500_000 calls of iterative `fib(45)` with an opaque argument | Same integer |
| `sort` | Selection sort, `n=16000`, checksum of the sorted buffer | Same integer |
| `alloc` | `zeros` + 25 fills, `n=4_000_000`, checksum | Same integer |
| `parallel` | Independent fill + sequential reduction, `n=20_000_000` | Same integer |

`parallel` on YUKT may use automatic `repeat` parallelization. That is **not** a `start` / `wait` kernel. C / C++ / Rust / Go must provide an explicit sequential row and, where they already do in 0.11, an explicit-thread row. Label them `seq` and `par`. Do not call YUKT auto-par “the same” as `std::thread` without the label.

### 4.2 Task kernels (new; see §9)

| Kernel | Size / algorithm | Expected stdout |
| --- | --- | --- |
| `tasks_forkjoin` | `N` independent integer jobs, `start` / `wait` (or the class-labeled equivalent), reverse-order wait, checksum of results | Same integer **within a comparison class** |
| `tasks_nested` | Nested `start` of bounded depth (8), integer result | Same integer within a class |

`N` is locked at implementation time to a value whose YUKT median is ≥ 1 ms on both publication hosts. Record `N` in the JSON. Do not pick `N` after seeing which language wins.

### 4.3 What is not a timed kernel

- The usability program (records + `result` + `std/file` or `std/json` + `start` / `wait`) is a **correctness / compile** check, not a speed claim.
- Worker-trap and double-wait tests stay in CTest. They are not benches.

## 5. Measurement

**What is timed:** in-process kernel compute, not process startup, not `yukt run` driver overhead.

Contract for every language:

1. The process starts.
2. **Warmup:** the kernel function runs once. Its stdout may be discarded (YUKT 0.11 redirects warmup stdout to `/dev/null` in `benches/driver.c`).
3. **Timed:** the kernel function runs a second time. A monotonic clock (`CLOCK_MONOTONIC` / `steady_clock` / `Instant` / Go `time.Now()` + `time.Since()`) bounds only that call. Do not use `runtime.nanotime`.
4. The timed call prints the checksum on **stdout** and `compute_ns=<integer>` on **stderr**.
5. The harness starts **15** separate processes per binary. The published time is the **median** of those 15 `compute_ns` values.

Statistics stored per language row: `median_ns`, `min_ns`, `max_ns`, `q1_ns`, `q3_ns`, `iqr_ns`, `mad_ns`, `spread_ns`, and the raw `runs` array of 15 integers.

YUKT kernel binaries keep the 0.11 shape: LLVM `@main` is renamed, `benches/driver.c` calls `yukt_main` twice. That isolates CRT/`yukt_runtime_release` from the timed region. C / C++ / Rust / Go must call the same kernel function twice in one process. Timing `main` from process start is invalid.

**Dead kernels.** If any language’s median is **under 1 ms**, the harness must print `no speed claim` for that kernel (the compiler likely folded the work). The run may still be stored.

**Faster-than rule (unchanged from 0.11).** A language is only called faster on a kernel when:

- every compared row has `checksum_match == true`
- every compared median is ≥ 1 ms
- its median is at least **5%** below the next language in the **same comparison class**

Otherwise the line is `too close to call`. A successful run that supports no 5% win still has value; publish it.

Do not report means as the headline. Do not drop outliers by hand. If a run is thrown out, the reason is a failed checksum or a non-zero exit, and that binary’s row is `invalid`, not a quieter median.

## 6. Correctness

Performance is not accepted until stdout matches.

1. Compile all language variants of a kernel.
2. Run them under the measurement contract.
3. Compare **timed-call stdout** (not warmup). All languages in the claim set must print the same bytes.
4. If any mismatch: record `checksum_match: false`, print the four/five stdout blobs, and **forbid** a speed claim for that kernel.
5. A non-zero exit is a failed run, not a slow run.
6. Integer overflow in these kernels must not be the interesting result. Sizes are chosen so the checksum fits in signed 64-bit without overflow for the locked algorithms.

YUKT numerical rules stay as in `docs/LANGUAGE_SPEC.md`: signed overflow is undefined; this suite avoids it. Do not add float reductions to the first 0.15 claim set.

## 7. Results files

| File | Rule |
| --- | --- |
| `benchmarks/results-0.11.json` | Frozen. `verify-release.sh` already checks it is unchanged vs HEAD. Do not overwrite |
| `benchmarks/results-0.11.csv` | Frozen |
| `docs/BENCHMARKS.md` | 0.11 table stays. 0.15 adds a **new section** after review, or a sibling `docs/BENCHMARKS_015.md` if the 0.11 page must stay byte-stable |
| `benchmarks/results-0.15.json` | **New.** One file with a `hosts` array, or one file per host (`results-0.15.macos-arm64.json`, `results-0.15.linux-x86-64.json`) plus a thin index. Prefer two host files plus an index so a Linux write cannot clobber a Mac write |
| `benchmarks/results-0.15.csv` | Optional export of medians only; JSON is the record |
| `benchmarks/results.json` | 0.11 harness still writes this locally. 0.15 must not silently replace it as the published freeze |

`results-0.15*.json` must include: method version (`0.15`), `yukt` version, host metadata, compiler flags, per-kernel raw runs, `checksum_match`, and a `limitations` array copied from §8.

A missing host is reported as `not measured`, not as a copy of the other host.

## 8. Reporting

Every published 0.15 section must state:

- CPU, OS, arch, `ncpu`
- Exact compiler versions and flags (§3)
- Kernel sizes and expected checksums
- Median, IQR, MAD, min–max for YUKT; medians for the others; raw runs in JSON
- Which comparison class each column belongs to (§9)
- The 5% and 1 ms rules
- That numbers are in-process kernel time, not `yukt run` wall time
- That 0.11 numbers are a different compiler version and must not be subtracted from 0.15 as “speedup” without saying so

**Limitations (keep visible):**

- Running-wait cycles can hang; 0.14.1 has no deadlock detector. Task benches must not construct a wait cycle.
- YUKT Linux tasks use **dedicated persistent pthread workers (≤32)**, not one `pthread_create` per `start`. Comparing that pool to thread-per-task C is not a YUKT-vs-C win.
- The heap is one recursive mutex; task bodies that allocate serialize on `yukt_heap_mu`.
- GitHub Actions times are not publication-quality.
- Automatic `repeat` parallelization is a compiler decision plus a runtime tax (`1000000` work, `100000 * ncpu`). It is not the task API.
- No GPU / CUDA numbers. Apple Silicon has no CUDA.

Do not write “YUKT is faster than C++” as a general sentence. The only allowed headline is the `winner_line` for a named kernel on a named host.

## 9. Task benchmarks

`start` / `wait` is a **pool fork-join**. Sequential C that does the same arithmetic on one thread answers “how much work is there?”, not “is YUKT’s pool fast?”.

Comparison classes (do not rank across classes):

| Class | Meaning | Allowed peers |
| --- | --- | --- |
| **A — persistent pool** | Workers exist before the timed `start`s; `start` enqueues | YUKT `start`/`wait`; **required** C worker pool (`ncpu` capped at 32, created before the timed region, joined at process end); optional C++/Rust pool with the same lock; Go `go` + `WaitGroup` (runtime scheduler; label as Go scheduler, not “pthread pool”) |
| **B — thread per task** | One OS thread created per job | C `pthread_create`/`join`; C++ `std::thread`; Rust `std::thread` |
| **C — sequential** | One thread, no spawn | All five languages, same arithmetic |

Rules:

1. Publish Class A, B, and C as **separate columns**.
2. A speed claim may only compare rows inside one class.
3. Class C is required. It is the work lower bound and a checksum twin. It is not “YUKT lost to sequential C, therefore tasks are slow” unless Class A YUKT is also slower than Class A peers by the 5% rule.
4. Linux YUKT Class A must document persistent pthread workers, cap 32, separate from range-parallel `repeat` workers.
5. macOS YUKT Class A is libdispatch. Do not call it pthreads.
6. Do not use `repeat` auto-par as a stand-in for `start` / `wait`.
7. Wait order is locked (reverse of start order) so the inline-claim rule is exercised the same way in every language’s pool, if that language implements “run inline if still queued”. Languages that cannot express that rule still wait in reverse order.
8. Arguments and results are integers in the first suite (no heap `ref` across `start`; YUKT forbids that).

Class A **must** include a persistent C worker-pool implementation in the bench source: worker count = `ncpu` capped at 32, created before warmup, joined at process end, not created inside the timed region. C Class B (thread-per-task) and Class C (sequential) remain. Do not treat C Class B as the Class A peer.

## 10. CI

CI proves the **harness and checksums** on both platforms. It does not publish speed claims.

| Job | What it must do | What it must not do |
| --- | --- | --- |
| `macos-latest` (arm64) | Build YUKT; compile all language kernels that the runner has toolchains for; run each kernel once (or 15 if time allows); require `checksum_match` | Fail the release because a median moved; overwrite `results-0.11.json` |
| `ubuntu-latest` (x86-64) | Same | Same |

Toolchain on Ubuntu CI: distro `clang` / `llvm`, `rustc` via rustup or distro, `golang-go`. If a toolchain is missing, the job installs it or the kernel row is `skipped` with a reason. A skipped Go row is not a YUKT win.

Pinned method for a future workflow (not added until this spec is accepted):

- `ctest` stays the correctness gate it is today (40 tests at `v0.14.1`).
- A new job or step runs the 0.15 harness with `BENCH_RUNS=3` or `15` (record which).
- Checksums must match. Medians may be uploaded as artifacts named `results-0.15.ci-<os>.json`.
- Those artifacts must set `"publication": false`.

`scripts/verify-release.sh` keeps expecting `0.14.1` until a later version bump. It must continue to assert the 0.11 JSON hash and historical tags. It may later assert that `results-0.15*.json` exists and does not touch the 0.11 file.

Reproducible execution means: same source, same recorded flags, same algorithm, same checksum. It does not mean bit-identical `compute_ns` on two GitHub runners.

## Harness notes (when implementation is authorized)

- Extend the existing `benches/run.py` contract (`compute_ns`, 15 runs, 5%, 1 ms) rather than inventing a second clock.
- Add C and Go as first-class compile/measure paths.
- Write `benchmarks/results-0.15.<host>.json`, never `results-0.11.json`.
- Keep `benches/driver.c` warmup/timed split for YUKT.
- Do not add reserved words. Do not change allocator caps.

## Approval

This method is the gate in `docs/YUKT_015_DESIGN.md`. The three review corrections (C++17, Go `time.Since`, required C Class A pool) are in this file. Harness work follows in later commits.
