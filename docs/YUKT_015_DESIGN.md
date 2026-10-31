# YUKT 0.15 design — Concurrency and systems maturity

Status: **proposed, not implemented.** No 0.15 syntax is in `docs/LANGUAGE_SPEC.md`. Do not implement mutex, channel, network, or async language features from this file. CUDA / GPU stay **after** 0.15.

0.14.1 shipped host-native `start` / `wait` / `task[T]` (`v0.14.1`). 0.15 makes YUKT useful for systems and backend work. It does not exist to grow the keyword list.

## What 0.15 must prove

1. **Performance.** Native codegen and the runtime stay competitive with C, C++, Rust, and Go on the same algorithms, measured on macOS arm64 and Ubuntu x86-64. Publish numbers, not slogans. Do not overwrite `benchmarks/results-0.11.json`.
2. **Safety.** Ownership and task rules stay enforced. Shared state, if added later, needs aliasing rules before syntax. Deadlock remains possible in 0.14.1; 0.15.3 may diagnose it, not silently “fix” it.
3. **Usability.** A developer can build a real host program without fighting the language. A feature that only exists in the compiler and never appears in a small application does not count.

## First gate (before new language)

Establish a **compatibility and benchmark suite** against C, C++, Rust, and Go.

Today `benches/` compares YUKT to C++ and Rust on compute kernels (0.11 freeze). That is not enough. The 0.15 suite must also cover:

| Axis | What to add | Must not do |
| --- | --- | --- |
| Performance | Same-algorithm kernels plus Go; both CI hosts; new `benchmarks/results-0.15.json` | Overwrite 0.11 results; claim “faster than X” without a 5% median gap |
| Safety | Ownership + task stress that CI runs on Linux and macOS | Treat 40 CTests as a thread-safety proof |
| Usability | A small multi-file program using records, `result`, `std/file` / `std/json`, and `start` / `wait` | Invent HTTP or mutex syntax to make the demo work |

This gate is independently releasable. It adds no reserved words.

## Proposed slices (not specified)

| Slice | Focus | Ships only after LANGUAGE_SPEC | Must not require |
| --- | --- | --- | --- |
| **0.15.1 Synchronization** | Mutexes and safe shared state | Alias / `ref` rules across tasks | Channels, async, CUDA |
| **0.15.2 Channels** | Typed communication between tasks | A channel type and move rules | Shared `ref` without 0.15.1 |
| **0.15.3 Deadlock safety** | Task-dependency diagnostics | A stated hang policy | A runtime that “never deadlocks” |
| **0.15.4 Networking** | TCP, UDP, HTTP foundations | Types or `std` modules in the spec | Async syntax |
| **0.15.5 Async foundations** | Async I/O and runtime design | A written I/O model | CUDA |
| **0.15.6 Performance** | Allocator caches, benches, profiling | Measurement first | New syntax |

Apple Silicon has no CUDA. Do not claim GPU execution on a Mac. Do not start a CUDA slice to postpone the measurement gate.

## What 0.14 already froze (do not weaken)

- `start` / `wait` / `task[T]`; move-only args and results; `ref` cannot cross `start`
- Linux task pool: dedicated persistent pthread workers (≤32), not one thread per `start`
- Running-wait cycles can hang; no timeout or detector in 0.14.1
- One heap mutex; 8,948,352-byte cap; six classes; four slots
- Frontend stays LLVM-free
- `benchmarks/results-0.11.json` frozen

## Approval

These slices are a **planning sketch**. Implementation of 0.15.1+ is not authorized until the corresponding surface is copied into `docs/LANGUAGE_SPEC.md`. `docs/BENCHMARK_METHOD_015.md` is **accepted**. Harness implementation may extend `benches/`. Do not overwrite `benchmarks/results-0.11.json`.
