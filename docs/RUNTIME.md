# YUKT runtime

Contract for `runtime/yukt_runtime.c`, frozen in 0.11. The compiler frontend must not include this header’s implementation details; generated programs link `libyukt_runtime.a`.

## Size-class payload cache

After `yukt_main` returns, `yukt_runtime_release` frees every tracked list header, string, and oversize payload. It **retains** payload buffers that fit a size class so a later `zeros`, list allocation, or string concat can reuse them. Reused buffers are zeroed. This is not a GC and not intra-function free.

### Size classes

Six classes, in bytes:

| Class | Capacity |
| --- | --- |
| 0 | 16 |
| 1 | 64 |
| 2 | 256 |
| 3 | 4096 |
| 4 | 65536 |
| 5 | 1048576 (1 MiB) |

An allocation of `n` bytes is rounded **up** to the smallest class that fits. A request larger than 1 MiB is not cached (`malloc` / `free` for that block).

### Slot limit

Each class has **four slots**. Lists and strings use **separate** caches (`yukt_list_bins` and `yukt_str_bins`). Putting a block into a full class fails and the block is `free`d.

A cached 16-byte slot is never handed to a 64-byte request: take uses the same size class as the rounded request.

### Maximum retained memory

Sum of class capacities = 16 + 64 + 256 + 4096 + 65536 + 1048576 = **1,118,544 bytes**.

Maximum retained payload bytes after release:

```
4 slots × 1,118,544 bytes × 2 caches (list + string) = 8,948,352 bytes
```

That is an upper bound on **cached payloads**. Tracker nodes and live objects during `yukt_main` are extra and are not part of this cap.

### Thread safety

The caches and the allocation tracker are **not locked**. The 0.11 compiler refuses to parallelize a `repeat` whose body allocates (`zeros`, list literals, string concatenation). Parallel fills only write existing payloads. Parallel reduction combines use `yukt_reduce_mu`.

## Parallel reductions

`yukt_reduce_i64` / `yukt_reduce_f64` combine chunk partials under a mutex. Signed integer overflow in the combine is undefined (C `int64_t`). Float combines follow the language rule: association and rounding may differ by chunking and CPU count. See `docs/LANGUAGE_SPEC.md` (Numerical semantics).

## Record list elements (0.13.1)

`YuktList.kind == 5` stores record values packed at `YuktList.stride` bytes per element. Payload allocation still uses the size-class cache and 16-byte alignment. Index get/set copies `stride` bytes and traps on an out-of-range index. This does not change allocator caps, slot counts, or parallelization rules. Record values themselves are not heap objects.

## Standard-library primitives (0.12.1)

`std/str`, `std/list`, `std/sort`, `std/math`, and empty-list traps in `std/stat` call C helpers in this runtime. They use the same tracker and size-class caches. They do not free during `yukt_main`. Generated programs link `-lm` for libm.

| Prefix | Role |
| --- | --- |
| `yukt_str_*` | Byte length, index, slice, find, split/join, repeat |
| `yukt_list_copy` / `fill_*` / `reverse` / `slice` | Polymorphic fixed-length list ops |
| `yukt_list_sort_*` / `sorted` / `search` | Unstable numeric sort and binary search |
| `yukt_math_*` | Integer helpers and libm wrappers |
| `yukt_stat_min_*` / `max_*` / `mean_f` | Empty-list traps; sums stay in YUKT |

## Ownership release (0.13.2)

`yukt_release(void* ptr)` unregisters one tracked list or string and returns
its payload to the size-class cache or `free`. Lists drop owned elements
first. The compiler inserts this call. `yukt_runtime_release` still runs
when `yukt_main` returns and releases anything still registered.
Caps, six classes, and four slots are unchanged.

Releasing null is a no-op. Releasing a pointer that is not registered, or a
live object twice, traps. String literals are copied into tracked buffers when
they become owned values; the static literal is never released. See
`docs/LANGUAGE_SPEC.md` (0.13.2 — Ownership).
