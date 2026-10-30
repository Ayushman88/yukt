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
