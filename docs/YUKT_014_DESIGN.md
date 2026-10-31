# YUKT 0.14 design — Concurrency foundations

Status: **0.14.1 released** (`v0.14.1` = `df4fda9`). Language surface is in `docs/LANGUAGE_SPEC.md`. CUDA / GPU stay after 0.15. See `docs/YUKT_015_DESIGN.md`. Type construction uses field assignment so GCC `-Werror=missing-field-initializers` does not fire.

0.14 is a **language** milestone. A thread pool already exists for automatic `repeat`. 0.14.1 adds an explicit, typed way to run a function on that pool and wait for its value. It must not pretend implicit parallel loops are a task API.

The 0.14.1 syntax lives in `docs/LANGUAGE_SPEC.md`. This file keeps implementation notes.

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
| **After 0.15** | GPU interface with CPU fallback; CUDA on Linux+NVIDIA only | 0.15 measurement and systems slices |

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

## Later work (not specified here)

Moved to `docs/YUKT_015_DESIGN.md`: mutex / shared `ref`, channels, deadlock diagnostics, networking, async, allocator caches. CUDA / GPU stay after 0.15.

## Approval

Language surface, runtime, and gate: **accepted in design review (2026-10-31).**  
Written spec: `docs/LANGUAGE_SPEC.md` § 0.14.1. Implementation follows `docs/superpowers/plans/2026-11-01-0141-cpu-tasks.md`.

## Implementation notes (not language changes)

These notes freeze **how** 0.14.1 must be built. They do not add cancellation, shared `ref`, mutex types, atomics as a language feature, async/await, channels, or GPU. If a note appears to change user-visible meaning, the LANGUAGE_SPEC / sections above win and this note is wrong.

### 1. Atomic task claiming and lifecycle

Exact states and the only allowed transitions:

```
Queued → Running → Completed → Consumed
```

- `start` constructs a tracked task object in **Queued** and enqueues it.
- Exactly one executor claims **Queued → Running**. The claim is a single atomic compare-exchange (or the pool mutex plus a state word updated only while that mutex is held). A task must not execute twice.
- The worker or inline waiter stores the result, then **Running → Completed**.
- `wait` that observes **Completed** copies/moves `T` out and **Completed → Consumed**, then releases the wrapper.
- There is no transition back. **Consumed** is terminal.

Claim candidates: a pool worker, or the thread executing `wait` on that same task. No third party.

### 2. Race between a worker and inline `wait`

`wait t` and a worker may both see **Queued**.

1. Both attempt `Queued → Running` with the same atomic claim.
2. The winner executes the function on **its** thread.
3. The loser:
   - if it is `wait`: it does **not** execute. It waits for **Completed**, then consumes.
   - if it is a worker: it must not execute. It returns to the pool.

`wait` runs the body inline **only** if it wins the claim while the state is still **Queued**. If a worker claims first, `wait` blocks. This is the approved “if the job has not started, wait runs it” rule, stated as an atomic race rather than a TOCTOU check.

### 3. Nested tasks and worker starvation

A started function may call `start`. That enqueues another job on the **same** host pool (libdispatch on macOS, the existing pthread pool on Linux). It does not create a thread.

0.14.1 `wait` does **not** steal **other** queued jobs while blocked. The only inline execution is the waited-for task, and only when `wait` wins the **Queued → Running** claim.

A waiting worker that blocked on a **Running** task stays blocked. Work-stealing while waiting is deferred to a later slice.

Nested `start` + `wait` makes progress because:

- if the inner task is still **Queued**, the waiter claims and runs it on the current thread;
- if the inner task is **Running** on another worker, the waiter blocks until **Completed**.

### 4. Waiting on an already-running task

If `wait t` observes **Running**, it waits on a per-task completion object (condition variable, futex, or `dispatch_group` / semaphore). It does not spin.

Deadlock case: every pool worker is blocked in `wait` on tasks that are **Running** on other workers, and those workers are themselves blocked. 0.14.1 does not add a deadlock detector. The inline-claim rule prevents the common “wait for a still-queued child” pool-starvation. Programs that create a wait cycle among **Running** tasks can hang. That is accepted for this slice. No timeout.

### 5. Result storage alignment and lifetime

The task wrapper is one tracked heap allocation (`YUKT_ALLOC_OBJ` via `yukt_track_obj` / `yukt_013_free`, or an equivalent tracked kind). It contains:

- atomic state
- callee pointer and moved argument storage
- completion object
- a result slot whose size and alignment are supplied by the compiler at `start` (at least `max_align_t` when `T` is not void; 0 when `T` is void)

The result stays in that slot until `wait` consumes it. Alignment of `T` (including records and `result[U]`) must match the host ABI used for ordinary `give`. The waiter memcpy / load of `T` happens **before** the wrapper is released. After **Consumed**, the result bits in the wrapper are dead.

### 6. Task wrapper destruction after `wait`

`wait` is the unique owner of the wrapper at consume time:

1. Claim the result (state must be **Completed**; this thread performs **Completed → Consumed**).
2. Move `T` into the waiter’s SSA / local (or discard if void).
3. Call `yukt_release` on the wrapper (which untracks and frees / caches it).

Dropping a task without `wait` is a compile error (`Unused task`), including overwrite. The compiler must not insert a silent `yukt_release` of an unconsumed task as if it were an unused string: that would leak a running job or drop a result. If a later slice allows ignored tasks, it needs a new spec.

### 7. Allocator and allocation-tracker synchronization

Today (`runtime/yukt_runtime.c` on `d296d80`):

- `yukt_allocs` tracker, `yukt_list_bins`, and `yukt_str_bins` are **not** locked. The comment says they are single-threaded because parallel `repeat` refuses allocating bodies.
- `yukt_reduce_mu` only protects reduction accumulators.
- Linux `yukt_par_mu` only protects the range-parallel job array.

0.14.1 workers **do** allocate. One runtime mutex (`yukt_heap_mu`) must cover **every** path that touches:

- `yukt_track` / `yukt_untrack` / `yukt_debug_live_allocs`
- `yukt_cache_take` / `yukt_cache_put` (list and string bins)
- `yukt_alloc_aligned_zero` / `yukt_str_alloc` / list header allocation
- `yukt_release` and `yukt_013_free`
- map / set / JSON alloc/free in `runtime/yukt_013.c` that use the tracker or the size-class caches

A mutex around `malloc` alone is not enough. `yukt_reduce_mu` may be reused **only** if every heap path takes it; otherwise introduce `yukt_heap_mu` and keep `yukt_reduce_mu` for reductions. Caps stay 8,948,352 / six classes / four slots. Caps are checked under the same mutex.

### 8. Runtime shutdown with outstanding tasks

`yukt_runtime_release` is invoked from LLVM lowering only on `main` return (`compiler/backend/llvm_backend.cpp`). Unused tasks are compile errors, so a well-typed program should have no **Queued** / **Running** / **Completed** task at that point.

Before tracker teardown the runtime must still:

1. `yukt_task_shutdown`: refuse new `start`; wait until the queue is empty and no task is **Running** (or abort if any remain — implementation may drain **Queued** jobs on the shutting-down thread).
2. On Linux, do not set `yukt_par_stop` or join pool threads until task drain completes. The existing pthread pool has `yukt_par_stop` but nothing currently sets it.
3. Then `yukt_runtime_release`.

Process-exit cleanup while a worker can still `yukt_track` is a use-after-free. Shutdown is sequenced: drain → join or idle the pool → release tracker.

### 9. Worker traps and process abort

`die()` in `runtime/yukt_runtime.c` prints `YUKT runtime error:` and `exit(1)`. Worker traps (bad slice, double release, untracked pointer) use the same `die()`. They do not become `fail`. They do not unwind to `wait`. The process aborts. No new error channel.

### 10. Task transfer through arguments and returns

`task[T]` is a heap owner. `type_owns_heap` must return true for it.

- `u = t` moves; `t` becomes Moved.
- `give t` moves; callee must not drop.
- A parameter of type `task[T]` is Owned (moved in), same as `string` / `map`.
- `wait` is the consume. A moved-from name is `Use after move`.
- `ref task[T]` is a 0.13.3 borrow of a **place**. Passing that `ref` into `start` is `Cannot start a ref`. A function may take `ref task[T]` on one thread (e.g. to wait after a helper inspects it) only if `wait` still consumes the owner; 0.14.1 should reject `wait` of a `ref` and require the owned name. Locked: `wait` needs an owned `task[T]` rvalue / moved name, not a `ref`.

### 11. Planned-parallel `repeat` detection

The compiler, not the runtime, decides. Existing predicate: `Lowerer::can_parallelize` in `compiler/ir/lower.cpp` (`worth_parallelizing` with threshold `1000000`, `stmts_allocate`, `collect_loop_effects` independence, not already `in_parallel_`). Runtime `yukt_par_should_run_sequential` (`100000 * ncpu`) must **not** be the gate.

Circularity to avoid: `start` allocates a task and is a user call. Today `has_user_call` / `stmts_allocate` would make a loop containing `start` sequential, which would then allow `start`. That is the wrong test.

Locked rule: compute the planned-parallel decision on the loop **as if `start` / `wait` were absent** (ignore those AST nodes for independence, allocation, and work). If that decision is `parallel == true`, any `start` in the body (including nested `if` / `repeat`) is `Cannot start inside parallel repeat`. If the decision is sequential (trip/work too small, body allocates for other reasons, not independent), `start` is allowed.

Do not reject `start` in every `repeat`. A 3-iteration loop may `start`.

`wait` inside a planned-parallel body is also forbidden in 0.14.1 if `start` is (the body cannot create a task there). A `wait` of a task started **outside** the loop is a user call / heap consume and already fails independence or “cannot move inside repeat.” Keep that.

### 12. Module-qualified `start` lookup

`start alias.name(...)` uses the same resolution as `analyze_call` in `compiler/semantic/analyzer.cpp`:

- qualifier must be a `use` alias
- name must be a **shared** function of that module (`lookup_shared`)
- private functions: `Unknown function` with the existing shared-name note
- shared records: constructors are not startable (`Invalid start`)
- `foreign`: `Invalid start` even if shared (and 0.13.4 already forbids `share foreign`)
- unqualified `start name(...)` uses `lookup_function` in the current file, then builtins. Builtins, `ok`, `fail`, `count`, `zeros`, and `main` are `Invalid start` or the existing unknown-function path
- stdlib runtime wrappers (`analyze_stdlib_call` / `is_stdlib_runtime_ir`) are ordinary shared functions and **may** be started if they have no `ref` parameter

### Compatibility impact (reserved words)

Approved: reserved words. Current tree identifiers named `start`:

- `std/str.yukt` `slice(s, start, end)`
- `std/list.yukt` `slice(src, start, end)`
- `tests/ownership/list_slice_owned_trap_start.yukt` local `start`

`wait` is unused as an identifier. Implementation must rename those `start` parameters/locals. Contextual keywords are not authorized.

### Out of scope (repeat)

No cancellation, timeout, mutex language type, atomic language type, channels, async/await, CUDA, or GPU.
