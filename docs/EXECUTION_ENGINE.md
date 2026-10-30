# YUKT execution engine

How a `repeat` loop becomes sequential LLVM, vectorized LLVM, or a runtime parallel range. Syntax is unchanged; this document is the compiler/runtime contract for 0.11.

## Layers

1. **YUKT IR lowering** decides whether a loop is independent (or a simple +/* reduction) and whether estimated work justifies outlining.
2. **LLVM O3** may vectorize, unroll, inline, or eliminate the resulting loops.
3. **The runtime** may ignore a `yukt_parallel_for` call and run the range on the calling thread.

`--explain` covers layers 1 and 2, plus the host triple. Layer 3 is dynamic.

## Independence

A loop is independent only if every iteration could run in any order:

- no `say`, `give`, or user function call
- list writes use an index that mentions this loop’s iterator
- scalars assigned in the body are not used after the loop, except a single simple reduction

Otherwise the loop stays sequential.

## Simple parallel reductions

One accumulator of integer or float type may be reduced in parallel when every assignment is:

```
acc = acc + expr
acc = acc * expr
```

or the commuting `expr + acc` / `expr * acc`, and `expr` does not mention `acc`. Each outlined chunk starts from the operator identity (0 for `+`, 1 for `*`) and combines into `acc` with a mutex after the chunk. `-`, multiple accumulators, reductions inside `if`, and loops that allocate (`zeros`, list literals, string concat) stay sequential.

Signed integer overflow is undefined (`nsw` in LLVM). YUKT does not wrap. Integer `+`/`*` without overflow match sequential results. **Float `+`/`*` reductions are not bitwise stable:** parallel chunking (one partial per CPU, then combine) and LLVM vectorization may change association and therefore **rounding**. Results may differ by chunking and CPU count. That is specified, not a bug. See `docs/LANGUAGE_SPEC.md` (Numerical semantics).

## Compile-time parallel plan

Work ≈ trip × (1 + assignment cost + 2 × index-assignment cost + nested body work).

Unknown nested inner trip counts contribute **256** iterations (was 64 in 0.10). `count(xs)` folds when `xs` is a list literal or `zeros(n)` with constant `n`.

Parallelize when:

- the loop is independent or a simple reduction, and
- it is not already inside a parallel region, and
- either trip is unknown, or `work ≥ 1_000_000`, and
- if nested inside another `repeat`, trip is a proven constant ≥ 1_000_000

Unknown trip counts still outline. The runtime sees the real `end - start + 1` and the compile-time `work_per_iter`.

## Runtime adaptive parallelization

```
yukt_parallel_for(start, end, fn, ctx, work_per_iter)
```

Run `fn(start, end, ctx)` on the caller if any of:

- `end < start`
- `count < 64`
- `ncpu ≤ 1`
- `count * work_per_iter < 100000 * ncpu` (overflow → treat as large)

Otherwise split into one inclusive chunk per CPU (libdispatch on macOS, persistent pthread pool on Linux).

## Vectorization

YUKT does not emit NEON/SVE/AVX by hand. It:

- lowers inclusive `repeat` to `k < niter`
- uses NSW integer arithmetic
- GEPs i64/f64 list payloads
- aligns payloads to 16 bytes and assumes that alignment
- marks latches `llvm.loop.mustprogress` and `yukt.iterator`

LLVM’s vectorizer then either produces `vector.body` / `llvm.vector.reduce` or it does not. `--explain` only says “vectorized” in the second case.

## Memory

Lists are heap `YuktList { length, kind, data }`. `data` is `aligned_alloc(16)` and zeroed.

When `yukt_main` returns, `yukt_runtime_release` frees every tracked object but keeps size-class payloads for the next `zeros`/`list`/string concat that fits. Buffers are allocated at bin capacity and zeroed on reuse. See `docs/RUNTIME.md` for the six size classes, four-slot limit, and the 8,948,352-byte retention cap.

The size-class cache and the allocation tracker are **not thread-safe**. Loops that allocate stay sequential so only the calling thread touches them. Reduction combine uses a mutex; list *payload writes* in an independent parallel fill do not allocate.

It is not a register-level store-forwarding pass and not intra-function GC. That waits for 0.13 ownership.

## Explanation mode

```
yukt --explain program.yukt
```

Header:

- host triple, host CPU, “native, not cross-compiled”
- cost-model thresholds (1e6 work, nest 1e6, runtime 64 / ncpu / 1e5×ncpu)

For each `repeat`:

- planned sequential or parallel, and why
- independence, nest depth, trip, work
- reduction name and operator when applicable
- **verified parallel** only if LLVM IR contains `yukt_parallel_for`
- **verified vectorized** only if `vector.body` or `llvm.vector.reduce` is in the relevant function or parallel thunk

Then a module-level dump of observed LLVM facts (assume, nsw, mustprogress, alwaysinline) and a list of transforms that were investigated but not applied as YUKT-IR passes.

Do not treat `--explain` as a profiler. It does not time loops.

## Reading a report

A large independent fill should say `planned: parallel` and `verified parallel: yes`.

`examples/hello.yukt`’s `repeat i in 1..5` should say sequential because the body contains `say`.

A huge `total = total + items[i]` may say `planned: parallel` with a reduction note. Other reductions stay sequential and may still be `verified vectorized: yes` if LLVM emitted a reduction vector loop.
