# YUKT execution engine

How a `repeat` loop becomes sequential LLVM, vectorized LLVM, or a runtime parallel range. Syntax is unchanged; this document is the compiler/runtime contract for 0.10.

## Layers

1. **YUKT IR lowering** decides whether a loop is independent and whether estimated work justifies outlining.
2. **LLVM O3** may vectorize, unroll, inline, or eliminate the resulting loops.
3. **The runtime** may ignore a `yukt_parallel_for` call and run the range on the calling thread.

`--explain` covers layers 1 and 2. Layer 3 is dynamic.

## Independence

A loop is independent only if every iteration could run in any order:

- no `say`, `give`, or user function call
- list writes use an index that mentions this loop’s iterator
- scalars assigned in the body are not reductions and are not used after the loop

Otherwise the loop stays sequential. Reductions such as `total = total + items[i]` are sequential on purpose. LLVM may still vectorize them (`llvm.vector.reduce`).

## Compile-time parallel plan

Work ≈ trip × (1 + assignment cost + 2 × index-assignment cost + nested body work).

Parallelize when:

- the loop is independent, and
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

This is the 0.10 answer to “is parallel execution worthwhile?” It uses iteration count, a complexity hint, CPU count, and a dispatch tax. It does not inspect cache locality at runtime.

## Vectorization

YUKT does not emit NEON/SVE by hand. It:

- lowers inclusive `repeat` to `k < niter`
- uses NSW integer arithmetic
- GEPs i64/f64 list payloads
- aligns payloads to 16 bytes and assumes that alignment
- marks latches `llvm.loop.mustprogress` and `yukt.iterator`

LLVM’s vectorizer then either produces `vector.body` / `llvm.vector.reduce` or it does not. `--explain` only says “vectorized” in the second case.

## Memory

Lists are heap `YuktList { length, kind, data }`. `data` is `aligned_alloc(16)` and zeroed.

When `yukt_main` returns, `yukt_runtime_release` frees every tracked object but keeps the largest list payload in a one-slot cache. The next `zeros`/`list` that fits reuses that buffer (still zeroed). This is reuse across program invocations of `yukt_main` in the same process — the benchmark harness’s warmup-then-time pattern benefits, and so does any host that calls `yukt_main` twice.

It is not a register-level store-forwarding or allocation-sinking pass.

## Explanation mode

```
yukt --explain program.yukt
```

For each `repeat`:

- planned sequential or parallel, and why
- independence, nest depth, trip, work
- **verified parallel** only if LLVM IR contains `yukt_parallel_for`
- **verified vectorized** only if `vector.body` or `llvm.vector.reduce` is in the relevant function or parallel thunk

Then a module-level dump of observed LLVM facts (assume, nsw, mustprogress, alwaysinline) and a list of transforms that were investigated but not applied as YUKT-IR passes.

Do not treat `--explain` as a profiler. It does not time loops.

## Reading a report

A large independent fill should say `planned: parallel` and `verified parallel: yes`.

`examples/hello.yukt`’s `repeat i in 1..5` should say sequential because the body contains `say`.

A checksum reduction should stay sequential even when the trip is huge; it may still be `verified vectorized: yes` if LLVM emitted a reduction vector loop.
