# YUKT optimization strategy

This is the long-term plan for making YUKT generate efficient native code without overfitting six kernels or growing syntax for performance.

## What 0.08 and 0.09 established

YUKT can already:

- Lower inclusive `repeat` to a counted `k < niter` loop LLVM can analyze
- Vectorize independent numeric list loops (NEON `ldp` / `smin` / fills)
- Auto-parallelize independent loops when estimated work is large
- Match explicit-thread C++/Rust on a 20e6 fill
- Match row-parallel C++/Rust on naive 512 ijk matmul

YUKT cannot yet claim general superiority. Two 0.08 headlines did not generalize:

1. **Matmul 5× vs C++** was automatic parallelism vs sequential ijk. Against a threaded C++/Rust ijk, YUKT is tied.
2. **Selection sort 2.5× vs C++** was LLVM 23 vs Apple clang 21. Against LLVM 23 C++, YUKT is slightly slower. Against `std::sort`, selection sort loses by about 700× on n=16000.

Future work should preserve those lessons: always ship sequential and parallel baselines, always match toolchain versions, always keep algorithm benches separate from compiler benches.

## Three execution modes (language later)

Do not add syntax in 0.10. When the language grows, three modes should exist so automatic decisions are measurable:

| Mode | Who decides | Use |
| --- | --- | --- |
| Sequential | Programmer | Peak single-thread codegen, debugging, tiny loops |
| Automatic | Compiler + runtime cost model | Default `repeat` |
| Parallel | Programmer | When the programmer knows the loop is worth threads |

Until those modes exist, Automatic is the only path. It must stay conservative: independence analysis first, then work estimate, then a runtime trip-count floor (currently 64).

## Cost model (Automatic)

Current (0.10):

- Independent iterations only (no reductions, no `say`/`give`, list writes mention the iterator)
- Work ≈ trip × (assigns + 2×index-assigns + nested trips)
- Parallelize if work ≥ 1_000_000, or trip is unknown at compile time
- Nested inside another `repeat`: only if trip is a proven constant ≥ 1_000_000
- Runtime: counts below 64, ncpu ≤ 1, or `trip × work_per_iter < 100000 × ncpu` stay sequential; one inclusive range per CPU; thread pool reuse
- `yukt --explain` verifies parallel/vector claims against LLVM IR

Next increments, still without syntax:

1. Detect reductions and emit a parallel reduction instead of forcing sequential `total = total + …`.
2. Prefer sequential SIMD when a constant-trip fill is smaller than ~few million and already vectorizes at VF×unroll covering a cache line.
3. A general loop-fusion / interchange pass with a dependence test, not per-benchmark patterns.

Never: special-case benchmark file names or literal sizes.

## SIMD and memory

Keep numeric i64/f64 list access as GEP. Do not reintroduce per-element runtime bounds checks on that path until there is a checked debug mode.

Next increments:

- Align 32 or 64 if SVE/wider vectors appear on a target; 16 is enough for NEON 128-bit
- Hoist list data pointers in YUKT IR so LLVM does not reload `YuktList.data` (partially done via `!invariant.load`)
- Optional `malloc` + fill instead of zeroing when the first use is a full overwrite (needs a store-forwarding analysis)
- A checked build that calls `yukt_list_get_*` so safety and speed are both testable

## Standard library vs compiler

The compiler should not recognize “this is a sort” or “this is matmul.” Those belong in a library:

| Routine | Compiler's job | Library's job |
| --- | --- | --- |
| Selection / counted loops | Vectorize, parallelize if independent | — |
| `std::sort` / introsort | Inline calls, no bounds in hot getters | Provide `sort` |
| Naive ijk matmul | Parallel outer `i`, vectorize `k` if legal | Provide blocked GEMM |
| Sum / reduction | Vectorize sequential reduction; later parallel reduction | Provide `sum` |
| Fill / `zeros` | Sequential SIMD or parallel fill by cost model | Provide `fill` |

0.11 should introduce library functions in YUKT (plus runtime only where the language cannot express it). Compiler benches keep naive algorithms. Library benches live under `benches/algo/`.

## Broader suite before 1.0

The six kernels are compiler microbenchmarks. A 1.0 suite should add, as real programs, not as IR special cases:

- JSON parsing and string splitting
- File I/O throughput
- Hash maps / histograms
- Recursion (tree walk, naive fib as a negative control)
- Large-n library sort
- Tiled matrix multiply
- Explicit multithreading once Parallel mode exists
- Allocator stress (many live lists, not one `zeros` + fill)

Each needs a checksum, a C++ and Rust counterpart, sequential and parallel variants where that is meaningful, and a record in `benchmarks/results.json`.

## Measurement rules

- Keep every previous milestone's table. Never overwrite 0.07, 0.08, or 0.09 numbers.
- Report median, IQR, MAD, min, max, checksum, and compiler flags.
- Same input sizes when comparing milestones, unless the old kernel was invalid (closed-form nested).
- Same algorithm in the compiler bench; label library algorithms separately.
- Compare YUKT against sequential **and** parallel baselines on the same LLVM where possible.
- A language is “faster” on a kernel only if its median is ≥ 5% below the next, and the kernel ran ≥ 1 ms.

## Non-goals

- Chasing a headline such as “faster than Rust”
- Adding keywords so a benchmark can opt into an optimization
- Interpreting YUKT to paper over codegen gaps
