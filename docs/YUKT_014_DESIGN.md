# YUKT 0.14 design — Concurrency foundations

Status: **0.14.1 CPU fork-join specified, not implemented.** 0.13.1–0.13.6 are on `main`. CUDA / GPU stay a later 0.14 slice. Linux x86-64 CI has not been re-run for 0.13.

0.14 is a **language** milestone. A thread pool already exists for automatic `repeat`. 0.14.1 adds an explicit, typed way to run a function on that pool and wait for its value. It must not pretend implicit parallel loops are a task API.

Do not implement 0.14.1 until this file’s syntax is copied into `docs/LANGUAGE_SPEC.md`.

## Goal

Let a YUKT program start a function on another worker, transfer ownership of arguments and the result, and wait. Host-native only. No GC. No shared mutation. No CUDA in this slice.

## What 0.13 already froze (do not weaken)

- Indentation, `create` / `say` / `give` / `if` / `repeat` / `record` / `ref` / `foreign` / `ok` / `fail`
- One owner per heap value; `ref` is a borrow of a place, not a thread share
- `result[T]` handling; unused result is a compile error
- Allocator caps in `docs/RUNTIME.md`; `benchmarks/results-0.11.json` frozen
- Frontend stays LLVM-free
- Automatic `repeat` parallelization and the host thread pool remain
- Maps, sets, and JSON are not thread-safe and stay illegal in a parallel `repeat` body

## Slice roadmap

| Slice | Ships | Must not require |
| --- | --- | --- |
| **0.14.1 CPU tasks** | `start` / `wait`, `task[T]`, move-only args/results, pool + alloc mutex | Mutex type, `ref` across threads, async, CUDA |
| **0.14.2+** (later) | Per-thread allocator caches; optional mutex; GPU interface with CPU fallback; CUDA on Linux+NVIDIA only | — |

Apple Silicon has no CUDA. Do not claim GPU execution on a Mac.

## 0.14.1 — CPU fork-join

Independently releasable. Programs that never write `start` or `wait` stay valid.

### Syntax

New reserved words: `start`, `wait`.

```yukt
create work(n):
    give n + 1

create main:
    t = start work(3)
    say wait t
```

Prints `4`.

- `start name(args)` is a **special call**, not a first-class function value. `name` is a known YUKT function in scope (same lookup as a normal call, including `alias.name`).
- `wait t` is an expression when `t` has type `task[T]` and `T` is not void. It is a statement when `T` is void.
- There is no `*` / `&`. The call site is `start work(3)`, not `start(&work)`.

### What may be started

| Allowed | Not allowed |
| --- | --- |
| Ordinary `create` / `share create` with a known signature | `foreign` |
| `create main` as a callee | `start main(...)` from any file |
| Nested `start` inside a started function | `start` of a local / constructor / `ok` / `fail` / builtin |
| | `start` inside a `repeat` the compiler plans to parallelize |

Wrong arity or argument types use the same diagnostics as a normal call.

### Type

`start work(...)` has type `task[T]` where `T` is the inferred (or annotated, if any) give type of `work`. If `work` never `give`s, `T` is void.

`task[T]` is an opaque owned heap type, like `map` / `set` / `json`. No fields. No constructor except `start`. No `say`.

`wait` of a non-task is a type error. `x = wait t` when `T` is void is a type error.

### Ownership

This is the 0.13.2 state machine, not a new one.

1. **Arguments.** Heap arguments to `start` are **moved** into the task, same as a normal call that takes them by value. After `start work(xs)`, `xs` is Moved. `ref` arguments to the callee are **not allowed** (`start` cannot borrow the caller’s place across threads).
2. **The task.** `t` is Owned. `wait t` moves `t`. An unused `task` is a compile error (same family as unused `result`). `give t` transfers the task. Overwriting `t` without `wait` is an unused-task error.
3. **The result.** `wait` moves `T` to the waiter. If `T` is `result[U]`, the waiter handles it with `if r.ok` as today.
4. **No shared mutation.** A `ref` cannot cross `start`. There is no mutex type in 0.14.1. Two tasks never own the same heap allocation.

### Runtime

- Reuse the existing host pool (libdispatch on macOS, persistent pthreads on Linux). `start` enqueues. It does not `pthread_create` per call.
- Alloc and free take **one** runtime mutex. Size-class bins, four slots, and the 8,948,352-byte cap stay as in `docs/RUNTIME.md`.
- A task object is a tracked heap allocation. `wait` releases the task wrapper after taking `T`.
- If the job has not started when `wait` runs, **`wait` runs it on the current thread**. If it is already running, `wait` blocks. No cancel. No timeout. This is the 0.14.1 deadlock rule (pool workers that `wait` must not pin the only workers).
- Nested `start` is allowed. `start` inside a planned-parallel `repeat` is a compile error so implicit and explicit concurrency do not nest in 0.14.1.
- A spawned body may allocate, call `ok` / `fail`, and use maps. Those objects are owned by that body until `give` / `wait`.
- Traps (bad slice, double release) still abort the process. They do not become `fail`.

### Diagnostics

| Situation | Title |
| --- | --- |
| `start` of `foreign` or `main` | `Invalid start` |
| `start` of an unknown name | `Unknown function` (existing) |
| Heap arg used after `start` | `Use after move` |
| `start foo(x)` where `foo` takes `ref` | `Cannot start a ref` |
| Unused task | `Unused task` |
| `say t` / `wait` of non-task | `Type mismatch` |
| `start` in a planned-parallel `repeat` | `Cannot start inside parallel repeat` |
| `x = wait t` when `T` is void | `Type mismatch` |

### What 0.14.1 does not do

- No mutex, atomic type, or `ref` across threads
- No `async` / `await`, channels, or first-class functions
- No CUDA, GPU types, or host/device copies
- No `std/array`, package manager, or extra `-l` flags
- No cancel, timeout, priority, or thread count API
- No `start` of `foreign`
- Allocator caps unchanged; `benchmarks/results-0.11.json` unchanged

### Regression gate (0.14.1)

- Previous 0.13 suite green on macOS arm64
- `start work(3)` / `wait` gives `4` for `create work(n): give n + 1`
- Heap argument is use-after-move at the caller after `start`
- Unused task is a compile error
- Two `start`s, `wait` in either order, both results correct
- Spawned body may allocate a string and `give ok(...)`; `wait` returns that `result`
- `start` inside a planned-parallel `repeat` is rejected
- Do not claim Linux until the 0.13 Linux job is green

## Later 0.14 slices (not specified here)

- Per-thread size-class caches (drop the alloc mutex)
- Mutex / shared `ref` if a later spec can state aliasing rules
- GPU execution interface with a CPU fallback
- NVIDIA CUDA prototype on Linux+NVIDIA only

## Approval

Language surface, runtime, and gate: **accepted in design review (2026-10-31).**  
Written spec: **this file.** Implementation waits for the same text in `docs/LANGUAGE_SPEC.md` and an implementation plan.
