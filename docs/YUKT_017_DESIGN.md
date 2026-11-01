# YUKT 0.17 design — Coordination and networking

Status: **accepted (human freeze 2026-11-01).** **0.17.1 implemented** (process I/O rules, channels, POSIX TCP, HTTP/1.1). Mutex remains specified, deferred. Language law is `docs/LANGUAGE_SPEC.md` § 0.17 and `docs/STDLIB_SPEC.md` (`std/sync`, `std/net`, `std/http`). This file remains the rationale.

0.17 adds **no new reserved words**. Surface lives in `std/sync`, `std/net`, and `std/http`. `start` / `wait` / `task[T]` stay exactly as 0.14.1.

**Read first:** `docs/LANGUAGE_SPEC.md` § 0.13.2 (ownership), § 0.13.3 (`ref`), § 0.13.5 (`result`), § 0.14.1 (CPU tasks), § 0.16.2 (stdin / argv / environment); `docs/YUKT_014_DESIGN.md`; `docs/RUNTIME.md` (allocator caps, `yukt_task_shutdown`).

**0.16.2 holes this document closes first:** concurrent `env.*` and stdin across `start` were unspecified. Those rules are frozen in §1 before channels, mutex, sockets, or HTTP.

---

## 0. What 0.17 is

A developer can coordinate `start` workers by **moving messages** on channels, open a POSIX TCP connection, and write a localhost HTTP/1.1 client and server. That is the proof, not a longer keyword list.

Independently releasable in slices:

| Slice | Ships | Must not require |
| --- | --- | --- |
| **0.17.1 channels + process I/O rules + TCP + HTTP/1.1** | §1, §2, §4, §5, §6–§8 as they apply | Mutex as a cross-task tool, TLS, UDP, bounded channels, `async` / `await` |
| **Later (specified here, not a 0.17.1 ship-gate)** | `std/sync` mutex API (§3) | A share box, `sync.shared`, or `ref` across `start` |

Programs that never call `std/sync`, `std/net`, or `std/http` keep 0.14.1 / 0.16 meaning. The §1 process-I/O rules apply to every program that already uses `std/io`, `std/env`, or `say`.

---

## What 0.14.1 and 0.16 already froze (do not weaken)

- `start` / `wait` / `task[T]`; heap arguments move; `start` cannot take `ref`
- Two tasks never own the same YUKT heap value
- Same host pool (libdispatch on macOS, persistent pthreads on Linux). No second scheduler
- If `wait` wins `Queued → Running`, it runs the body on the current thread; otherwise it blocks. No cancel. No timeout. Running-wait cycles can hang
- `start` inside a planned-parallel `repeat` is a compile error
- Allocator caps: six size classes, four slots, 8,948,352-byte retained-payload cap (`docs/RUNTIME.md`)
- One heap mutex for track/cache/alloc; do not hold it across a user body or a blocking wait
- Process exit: `yukt_task_shutdown` then `yukt_runtime_release`
- Traps abort the process (`YUKT runtime error:`). They do not become `fail`
- Recoverable I/O and parse failures are `result[T]` / `ok` / `fail`
- Function parameters are names only, except `name: ref T` (0.13.3)
- No field move-out from records. No tuples. No first-class functions. No closures
- `say` is the only stdout print. There is no `print`
- D1–D11 (growing lists) stay frozen

---

## 1. Process I/O versus `start` / `wait` (resolve first)

0.16.2 left stdin and environment races undefined. 0.17 does not “fix” that by passing `ref` of stdin or env into `start`. There is no stdin value and no env value.

**[D-IO] frozen.**

### 1.1 `io.program` / `io.args`

Captured once at process start (the same snapshot `yukt run` / the host `argv` already provide).

- Any thread may call them, including a started worker.
- Each call returns a **new owned** string / `list[string]`. Callers do not alias the process `argv`.
- Later `list.append` on an `io.args()` result does not change the process `argv` (0.16.2).
- These calls do not race with each other or with `start`.

### 1.2 Stdin (`io.read_line` / `io.read_all`)

**Origin-thread only.**

The origin thread is the thread that entered `yukt_main`. After `wait` returns to that thread, it is still the origin thread. A pool worker is never the origin thread.

- Two origin-thread reads stay sequential as today (0.16.2).
- A started worker that calls `io.read_line` or `io.read_all` **traps**: `stdin from worker`.
- The runtime test is **thread identity**, not “am I inside a `start` body.” If `wait` claims a still-queued task and runs it on the origin thread, that body may read stdin. If a worker claimed it, the same body traps.
- **Compile diagnostic (required when obvious):** `start name(...)` is `Cannot read stdin from a started function` when the static callee graph of `name` (whole-program, same 0.12 DAG used for ordinary calls) contains `io.read_line` or `io.read_all`. YUKT has no function values, so the graph is the named `create` / `share create` tree. A handler name of `http.serve` / `http.serve_on` is **not** a stdin edge of the function that only contains the serve form; see §5.6. The runtime trap remains a safety net.

Do not add an stdin handle. Do not let `start` take `ref` of stdin.

### 1.3 Environment (`env.get` / `get_or` / `has` / `set`)

Any thread may call them.

The runtime serializes every `env.*` call on **one internal process lock**. That lock is not a user mutex, not a `std/sync` value, and not shareable. There is no POSIX `getenv` / `setenv` data race.

- `env.set` is visible to the next `env.get` / `get_or` / `has` that acquires the lock.
- Empty-name traps and `fail` codes stay as 0.16.2.
- This is not a user-shareable object. It does not authorize sharing lists, sockets, channels, or any other heap value.

### 1.4 `say`

`say` remains the only stdout print. Do not invent `print`.

Today’s `yukt_say_*` / `yukt_print_*` paths are not serialized. **0.17 requires one internal process print lock** covering every `say` (and the C print helpers it uses). Each `say` holds that lock for the whole value plus the trailing newline. Two tasks may `say` concurrently; the lines do not interleave at byte granularity.

This lock is not a user mutex. It does not authorize sharing other state.

### 1.5 What this section does not do

- No `ref` of stdin or env into `start`
- No stdin multiplexer, no `io.read_line` from two workers
- No user-visible env object
- No change to `fail("eof")` / `fail("io")` / `fail("not_found")` codes

---

## 2. Channels first (0.17.1 ship-gate) — implemented.

Channels are implemented (0.17.1).

**[D-CH] frozen.** Coordination across `start` is by **moving** values through two owned ends. There is no shared list, no shared mutex, and no `sync.shared`.

### 2.1 Why not one channel value, and why not `(tx, rx)`

A single owned channel cannot be used by two tasks: `start` moves the argument, and `start` cannot take `ref`.

YUKT has **no tuple type** and **no field move-out** (`Cannot move out of record field`, 0.13.2). `sync.channel()` therefore cannot give `(tx, rx)` and cannot give a record from which the caller writes `tx = ends.tx`. Inventing tuple syntax or a one-off field-steal would reopen 0.13.2. Rejected.

**Frozen factory:** `sync.channel()` gives the **send** end. `sync.receiver(tx)` takes `ref` of that send end **on the same thread** and gives the **recv** end, once. After the split, the two ends are independent owners. Either end may be moved into `start`. Ends are not `ref`-shared across tasks.

```yukt
use std/sync

create worker(rx):
    r = sync.recv(rx)
    if r.ok:
        say r.value

create main:
    tx = sync.channel()
    rx = sync.receiver(tx)
    t = start worker(rx)
    sync.send(tx, 1)
    sync.close(tx)
    wait t
```

The program prints `1`.

### 2.2 Types

`send` and `recv` are builtin type names like `task` and `map`. They are **not** reserved words. A later identifier may be named `send` or `recv`. A **record** must not be named `send`, `recv`, `mutex`, `conn`, or `listener`.

| Type | Meaning |
| --- | --- |
| `send` | send end of a channel of inferred `E` (`send[E]` in this document) |
| `recv` | recv end of that same channel (`recv[E]`) |

No fields. No `say`. No constructor except `sync.channel` / `sync.receiver`. `E` is inferred from `sync.send` / `sync.recv` uses, the same way `map.new()` fixes `V`. `tx = sync.channel()` used only as `say count(...)` cannot happen here; a channel never used to send or recv is `Cannot infer type`.

`E` is any type that may be moved into a by-value function argument: plain values, heap values, records that contain ownership, `task[T]`, `result[T]`. Not `ref`. Not void.

### 2.3 Operations

Programs write `use std/sync`. First arguments that are ends are **borrows** of a place (same pattern as `list.append`), except `start`, which **moves**.

| Call | Gives | Effect |
| --- | --- | --- |
| `sync.channel()` | `send[E]` | Create a paired channel. Returns the send end. The recv capability is still inside it. |
| `sync.receiver(tx)` | `recv[E]` | Borrow `tx`. Extract the unique recv end. |
| `sync.send(tx, value)` | void | Borrow `tx`. Move heap `E` (copy plain `E`) into the unbounded buffer. |
| `sync.recv(rx)` | `result[E]` | Borrow `rx`. Block while empty and not closed. |
| `sync.close(end)` | void | Borrow a `send` or `recv`. Idempotent. |

`start` may take a `send` or `recv` **by move**, not `ref`. After `start worker(rx)`, `rx` is Moved.

### 2.4 Close, drop, and `fail("closed")`

- `sync.recv` on a closed end whose buffer is empty is `fail("closed")`. This is expected, not a programmer bug.
- `sync.recv` on a closed end with buffered values still gives those values, then `fail("closed")`.
- `sync.send` after the send end is closed **traps** (`send on closed channel`).
- `sync.send` after the recv end is closed or dropped **traps** (`send on closed channel`).
- `sync.close` on a send end: no more sends; recvs drain, then `fail("closed")`.
- `sync.close` on a recv end: remaining buffered `E` values are dropped; later recv is `fail("closed")`; later send traps.
- Close is **idempotent**. A second close is a no-op, not a trap.
- **Drop of an end without close is implicit close of that end.** Frozen (the recommended rule).
- Drop of the last end releases the runtime queue.

### 2.5 `sync.receiver` once

The analyzer tracks a Taken flag on a `send` name in the same function: `sync.receiver(tx)` sets it; a second `sync.receiver` on that name is `receiver already taken`. If extraction happens inside a callee through `ref`, the caller flag is not set. A second extract **traps** (`receiver already taken`).

`sync.receiver` after the send end is closed is still valid if it has not been taken: the recv end is born closed (recv is `fail("closed")` once empty).

### 2.6 Buffer and blocking

0.17.1 buffers are **unbounded**. `sync.send` does not wait for a receiver. A fast sender may grow until allocation failure, which **traps** (`out of memory`) like `list.append`. Bounded capacity is a later spec.

`sync.recv` blocks while the buffer is empty and the channel is not closed. No `try_recv`, `try_send`, timeout, or select in 0.17.1.

A recv/send wait cycle can hang. 0.17.1 has no detector (same acceptance as 0.14.1 running-wait cycles).

### 2.7 Internal queue is not a YUKT share

The two ends are two tracked YUKT heap allocations. The message queue they point at is a **runtime object** with an internal refcount (the same idea as the 0.14.1 task wrapper’s waiter + worker refs). Users cannot name the queue, borrow it, `start` it, or store it.

This is **not** a share type. It does not authorize two tasks owning one list, socket, or mutex.

Send **moves** `E` into the queue. Recv **moves** `E` to the waiter. Two tasks never own the same `E`.

### 2.8 One producer, one consumer per channel

0.17.1: one send end, one recv end. Ends are not cloned. Two producers need two channels, or one producer task that owns the only send end.

Ping-pong uses two channels:

```yukt
use std/sync

create bounce(rx, tx):
    repeat i in 1..1000:
        r = sync.recv(rx)
        if r.ok:
            sync.send(tx, r.value)
    sync.close(tx)

create main:
    to_w = sync.channel()
    back = sync.channel()
    rx_w = sync.receiver(to_w)
    rx_m = sync.receiver(back)
    t = start bounce(rx_w, back)
    repeat i in 1..1000:
        sync.send(to_w, i)
        r = sync.recv(rx_m)
        if r.ok:
            say r.value
    sync.close(to_w)
    wait t
```

### 2.9 Diagnostics (channels)

| Situation | Title |
| --- | --- |
| `start` of a function taking `ref send` / `ref recv` | `Cannot start a ref` (existing) |
| Use of a moved end | `Use after move` |
| Second `sync.receiver` on the same name | `receiver already taken` |
| Channel never sent or recvd | `Cannot infer type` |
| `say tx` / `say rx` | `Type mismatch` |
| `sync.send` / `recv` / `close` / `receiver` of the wrong end type | `Type mismatch` |

### 2.10 Parallel `repeat`

Channel operations allocate and/or block. A `repeat` body that mentions them is not independent and is not planned-parallel (same family as maps). `start` inside a planned-parallel `repeat` stays illegal.

---

## 3. Mutex (specified, deferred — not 0.17.1)

**[D-MU] frozen as API. Not a 0.17.1 ship-gate.**

```yukt
use std/sync

create main:
    m = sync.mutex()
    sync.lock(m)
    sync.unlock(m)
```

### 3.1 Rules

- `mutex` is a builtin type name (not reserved). Opaque owned heap value. No `say`. No fields.
- `sync.mutex()` gives a new unlocked mutex.
- `sync.lock(m)` and `sync.unlock(m)` take `ref` of a mutex place (borrow, same thread).
- The analyzer tracks a Locked flag on a mutex name in the same function. `lock` sets it; `unlock` clears it. Move, `give`, overwrite, or pass-by-value of a Locked name is `Cannot move a locked mutex`.
- If lock happens inside a callee through `ref mutex`, the caller flag is not set. Unlock of an unlocked mutex **traps** (`unlock of unlocked mutex`).
- Lock of an already locked mutex on the **same thread** traps (`mutex already locked`). 0.17 mutexes are not recursive. Cross-thread lock of a locked mutex would block — and cannot occur without a share (see §3.2).
- `start` cannot take `ref mutex` (existing `Cannot start a ref`).
- A mutex is not moved into a task for the purpose of sharing it. Moving an **unlocked** mutex into `start` transfers exclusive ownership; the other task does not keep a handle. That is a move, not coordination.

### 3.2 Why mutex is deferred

`start` cannot take `ref`. Two tasks cannot both borrow one mutex. A mutex therefore cannot coordinate two tasks unless this document invents a share box (`sync.shared`, refcount vs one-owner).

**Do not invent `sync.shared` in this freeze.** Specifying its ownership would reopen 0.13.2 (one owner per heap value). Default: channels first. Mutex is written so a later slice can implement the same-thread API, or a later design can add a share type without pretending 0.17.1 shipped cross-task locks.

CLI workers in 0.17.1 use channels, not shared memory.

### 3.3 What mutex does not do in this document

- No `lock` / `unlock` keywords
- No condition variables
- No recursive lock
- No `sync.shared`, atomics, or `ref` across `start`

---

## 4. Sockets (0.17.1)

**[D-NET] frozen.**

`use std/net`. POSIX TCP only (`AF_INET` / `AF_INET6`). No TLS. No UDP. No Unix-domain sockets.

### 4.1 Types

`conn` and `listener` are builtin type names (not reserved). Opaque owned heap values. Tracked allocations. They move like other heap values. They are **not** shared across `start`.

### 4.2 Address strings

One string, host and port:

| Form | Example |
| --- | --- |
| IPv4 | `127.0.0.1:8080` |
| IPv6 | `[::1]:8080` |
| Hostname | `localhost:8080` |

Hostname lookup uses `getaddrinfo`. A listen or connect uses exactly the family of the resolved address. No implicit v4-mapped dual-stack. Port `0` on listen requests an ephemeral port.

Malformed addr is `fail("io")` on the listen/connect call, not a trap.

### 4.3 Operations

| Call | Gives | Failure | Behavior |
| --- | --- | --- | --- |
| `net.listen(addr)` | `result[listener]` | `fail("io")` | Bind and listen. Does not accept. |
| `net.connect(addr)` | `result[conn]` | `fail("io")` | TCP connect. |
| `net.accept(ln)` | `result[conn]` | `fail("io")` / `fail("closed")` | Blocks until a connection or the listener is closed. |
| `net.addr(ln)` | string | — | Borrow. Bound `host:port` (IPv6 in brackets). New owned string. |
| `net.port(ln)` | integer | — | Borrow. Bound port. |
| `net.read(c, max)` | `result[string]` | `fail("io")` / `fail("closed")` | At most `max` bytes. |
| `net.write(c, data)` | `result[integer]` | `fail("io")` / `fail("closed")` | Write all of `data`, or fail. Ok payload is `str.len(data)`. |
| `net.close(x)` | void | — | Borrow a `conn` or `listener`. Idempotent. |

`max <= 0` **traps** (`invalid read size`). That is a programmer bug.

`net.read`: `ok(bytes)` of length `1..max`. Peer close with no bytes left is `fail("closed")`, not `ok("")`. Local close, then read or write, is `fail("closed")`.

`net.write` tries to send every byte. On failure the connection may be half-written; the payload is `fail`, not a partial count. Close it.

Drop of a `conn` or `listener` is implicit `net.close`.

`net.accept` after `net.close` on that listener is `fail("closed")`.

Listen backlog is implementation, not language.

### 4.4 Ownership

`start` may take a `conn` or `listener` **by move**. Two tasks never own the same connection or listener. There is no `ref` across `start`.

Read/write/close/accept borrow the place on the owning thread (or on the origin thread after `wait` if the owner never moved).

A `repeat` body that calls `net.*` allocates or blocks and is not planned-parallel.

### 4.5 Diagnostics (sockets)

| Situation | Title |
| --- | --- |
| `start` with `ref conn` / `ref listener` | `Cannot start a ref` |
| Use after move | `Use after move` |
| `say` of `conn` / `listener` | `Type mismatch` |
| `net.read` with non-integer `max` | `Type mismatch` |

---

## 5. HTTP (0.17.1)

**[D-HTTP] frozen.**

`use std/http`. HTTP/1.1 over the TCP in §4. No TLS (`https` is `fail("io")`). No HTTP/2. No chunked transfer. No keepalive: the server closes after one response; `http.get` sends `Connection: close` and reads one response.

### 5.1 Records

Shared records in `std/http`. Heap fields are 0.13.2-legal. Positional constructors, declaration order.

```yukt
share record Request:
    method: string
    path: string
    query: string
    headers: map
    body: string

share record Response:
    status: integer
    headers: map
    body: string
```

- `method` is the verb as received (`GET`, `POST`, …), ASCII as on the wire.
- `path` is the path without `?query`. Empty path in the request line becomes `/`.
- `query` is the raw query without `?`. Empty string if there was no query.
- `headers` is `map` of **string** values. Keys are lowercased. Duplicate names: last value wins. 0.17.1 does not keep multi-value headers.
- `body` is raw bytes as a YUKT string. No multipart parse.

Users who build headers write `use std/map`. `map.get(req.headers, "content-type")` borrows the field (no field move-out).

Do not require `say` of `Request` / `Response` in this slice (`headers` is an opaque `map`). Print `status` and `body`.

### 5.2 Client

| Call | Gives | Failure |
| --- | --- | --- |
| `http.get(url)` | `result[Response]` | `fail("io")` |

`url` is a string `http://host[:port]/path[?query]`. Default port is `80`. Userinfo, fragment, and `https://` are `fail("io")`. Connect, write, or parse failures are `fail("io")`.

0.17.1 `http.get` sends `GET`, `Host`, `Connection: close`, and no body. There is no `http.get_with` in this slice.

The ok payload is a **record** `Response`, not a bare string. A string-only client would drop status and headers that a small API client needs.

**Body framing (`http.get`).** After the response headers, `http.get` reads exactly `Content-Length` body bytes when that header is present and valid. If `Content-Length` is absent, `http.get` reads until TCP close. No chunked transfer. A `Transfer-Encoding: chunked` response is `fail("io")`.

### 5.3 Server: named `create`, not a closure

YUKT has no closures and no first-class functions. Parameters still have no ordinary type annotations (`req: Request` is **not** authorized; only `name: ref T` exists).

`http.serve` / `http.serve_on` are **compiler-recognized stdlib forms**, in the same family as `start`: the handler argument is a **callee name** in scope (unqualified or `alias.name`), not an expression and not a value.

Required handler shape, by inference from the serve form:

- one parameter, type `http.Request` (moved in)
- `give` type `http.Response` (moved out)

```yukt
use std/http
use std/map

create handle(req):
    headers = map.new()
    map.set(headers, "content-type", "text/plain")
    give http.Response(200, headers, "ok")
```

Wrong arity, a `ref` parameter, a `foreign` handler, or a give type that is not `Response` is a type error at the serve call (`Invalid http handler`). `main` is not a handler. The handler **may** be started as an ordinary function (`start handle(req)` is legal if `req` is a `Request` and `start` rules hold). The serve form is not `start`.

No new reserved word.

### 5.4 Serve operations

| Call | Gives | Behavior |
| --- | --- | --- |
| `http.serve(addr, Handler)` | void | Bind `addr`, then accept forever. Special form. Blocks. |
| `http.serve_on(ln, Handler)` | void | Move a `listener`. Accept forever. Special form. Blocks. |

`http.serve` is the brief’s `http.serve(addr, handler)`. `http.serve_on` exists so a test can `net.listen` + `net.addr` + `start` without racing bind, and without integer-to-string (YUKT has no `"http://" + port`).

**Bind / listen failure.** `http.serve` listen/bind failure **traps** with the named message `http bind failed`. The form is void and cannot return `result`. Programs that need `fail("io")` on bind use `net.listen` + `http.serve_on`. `http.serve_on` does not bind. An already-closed or otherwise unusable listener **traps** with the named message `http listener closed` (void form; cannot return `result`).

**Accept / handle.** 0.17.1 is strictly sequential on the serve thread: accept → handle → write → close → accept. The server does not `start` the handler. The handler may `start` its own work.

```yukt
use std/http
use std/net
use std/map

create handle(req):
    headers = map.new()
    give http.Response(200, headers, "ok")

create run(ln):
    http.serve_on(ln, handle)

create main:
    bound = net.listen("127.0.0.1:0")
    if bound.ok:
        ln = bound.value
        url = "http://" + net.addr(ln) + "/"
        t = start run(ln)
        r = http.get(url)
        if r.ok:
            say r.value.status
        wait t
```

`wait t` does **not** complete this program. The serve task stays in `accept` until process exit; `wait t` blocks until then. The example is not a program that returns from `wait` of a serve task. Unused `task` is still a compile error, so the `wait` stays in the listing.

`http.serve` / `http.serve_on` return only on listener close or process exit. 0.17.1 has no `http.shutdown`. The localhost stress test may leave the server blocked in `accept` and let `yukt_task_shutdown` release the listener. A test that must `wait` the server task closes the listener from the **server** side (for example the handler that answers the probe call `net.close` on a listener it still owns — it does not, after `serve_on` moved it). Accepted: process exit is the 0.17.1 server stop. A follow-on may add shutdown without changing §5.1–§5.3.

### 5.5 Request parse and body

- HTTP/1.1 request line and headers.
- Nonempty body only when `Content-Length` is present and valid. No chunked encoding: a `Transfer-Encoding: chunked` request is treated as malformed.
- Missing `Content-Length`: empty body.
- Malformed request line or headers: the server does **not** call the handler. It writes `Response(400, empty map, "")` if possible, then closes that connection. The process does not abort.
- Allocation failure while reading a body **traps** (`out of memory`). There is no extra HTTP body cap in 0.17.1.

**Response write.** The server always writes `Content-Length` from `str.len(body)`. A handler `headers` entry for `content-length` is **overridden**, not rejected. No chunked encoding on the response.

### 5.6 Handler and tasks

The handler may `start` work and `wait` before `give` of the `Response`, subject to the 0.14.1 deadlock rule (inline claim if still queued; otherwise block). A trap in the handler aborts the process.

The server does not `start` the handler. It invokes the handler on the serve thread (the thread that accepted): origin if `http.serve` is in `main`; a pool worker if `http.serve_on` was `start`ed.

**Stdin callee-graph.** A handler name is **not** a stdin callee-graph edge of the function that merely contains the serve form. `start run(ln)` where `run` only contains `http.serve_on(ln, handle)` is not `Cannot read stdin from a started function` merely because `handle` reads stdin. Stdin rules in §1.2 apply to the **thread that runs the handler**: origin-thread stdin is allowed if `http.serve` runs in `main`; a handler that reads stdin on a worker (`serve_on` was `start`ed) **traps** `stdin from worker`. The compile diagnostic still applies when an ordinary `start` callee graph (not through a serve form) contains `io.read_line` / `io.read_all`, including `start handle(...)` of the handler as an ordinary function.

### 5.7 Diagnostics (HTTP)

| Situation | Title |
| --- | --- |
| Handler is not a known function | `Unknown function` |
| Handler arity / types / `ref` / `foreign` / `main` | `Invalid http handler` |
| First argument of `serve_on` is not a `listener` | `Type mismatch` |
| First argument of `serve` is not a string | `Type mismatch` |

---

## 6. Ownership (summary)

0.13.2 / 0.14.1 stay. 0.17 adds **no** share type.

| Value | Across `start` |
| --- | --- |
| list, string, map, set, json, record, `task[T]`, `result[T]` | move only (existing) |
| `send` / `recv` | move one end; the other end is a different owner |
| `conn` / `listener` | move only |
| `mutex` | move only if unlocked; cannot coordinate two tasks |
| stdin / env | not values; §1 |

`ref` still cannot cross `start`. Two tasks never own the same YUKT heap allocation. The channel queue and the HTTP listen fd are runtime objects, not YUKT names.

---

## 7. Errors

Recoverable failures stay `result[T]` / `ok` / `fail`. Traps stay for programmer bugs. `http.serve` bind/listen failure is a trap only because the form is void and cannot return `result`; it is not a new class of programmer-bug trap. Prefer `net.listen` + `http.serve_on` when the program must see `fail("io")`.

| Code / trap | When |
| --- | --- |
| `fail("closed")` | `sync.recv` after close and drain; `net.read` / `write` / `accept` on a closed object; peer TCP close with no bytes |
| `fail("io")` | Socket and HTTP I/O (including `net.listen` bind failure), bad URL, connect failure, malformed addr |
| trap `http bind failed` | `http.serve` listen/bind failure (void form; use `net.listen` + `http.serve_on` for `fail("io")`) |
| trap `http listener closed` | `http.serve_on` given an already-closed or unusable listener |
| `fail("eof")` / `fail("not_found")` / `fail("io")` on stdin/env | unchanged 0.16.2 |
| trap `stdin from worker` | stdin on a non-origin thread |
| trap `send on closed channel` | send after close or after recv drop |
| trap `receiver already taken` | second `sync.receiver` |
| trap `unlock of unlocked mutex` / `mutex already locked` | mutex bugs (when implemented) |
| trap `invalid read size` | `net.read` with `max <= 0` |
| trap `out of memory` | channel growth, I/O buffers |
| trap (existing) | worker slice / double release / stdin empty-name env, etc. |

Unused `result` is still a compile error. Traps abort the process. They do not become `fail`. They do not unwind to `wait`.

---

## 8. Runtime lifecycle

- **Same host pool.** `start` enqueues. Channels, `net.accept`, `net.read`, `net.connect`, and HTTP do not create a second scheduler. Blocking I/O may block a pool worker. That is accepted in 0.17.1 (same class as blocking `wait`).
- **Allocator caps frozen.** Six classes, four slots, 8,948,352-byte retained cap. `benchmarks/results-0.11.json` unchanged.
- **Heap mutex.** Alloc, free, and tracker operations take `yukt_heap_mu`. Do not hold it across `sync.recv`, `net.accept`, `net.read`, `net.connect`, `http.get`, or a user `start` body.
- **Internal locks (not user mutexes):** env lock (§1.3), say lock (§1.4), plus per-channel wait objects and socket close flags.
- **Tracked allocations:** `send`, `recv`, the channel queue as an **internal payload** (not a second user owner), `mutex` (when built), `conn`, `listener`, HTTP parse buffers that are YUKT strings/maps. Channel ends, listeners, and connections are tracked.
- **Process exit:** `yukt_task_shutdown` (refuse new `start`; drain or abort running tasks as 0.14.1 already requires) **then** `yukt_runtime_release`. Open channels, listeners, and connections still owned at that point are released there (implicit close, then untrack).
- **`start` inside planned-parallel `repeat`** stays illegal.
- Worker traps still `die()` → `YUKT runtime error:` and `exit(1)`.

---

## 9. No new reserved words

| Kind | Names |
| --- | --- |
| Reserved words | none added (`start` / `wait` already reserved) |
| New builtin type names (not reserved) | `send`, `recv`, `mutex`, `conn`, `listener` |
| New `share record` in stdlib | `http.Request`, `http.Response` |
| New modules | `std/sync`, `std/net`, `std/http` |
| Special callee-name forms | `http.serve`, `http.serve_on` (like `start`, not keywords) |

A record must not be named `send`, `recv`, `mutex`, `conn`, `listener`, `task`, `map`, `set`, `json`, `string`, `list`, `integer`, `float`, or `boolean`.

---

## 10. Out of scope

Windows targets, CUDA / GPU, TLS, UDP, Unix sockets, `async` / `await`, first-class functions, closures, `std/array`, reopening D1–D11, 0.18 tooling (REPL, packages, formatter, LSP), bounded channels, select, timeouts, cancel, `try_recv`, recursive mutex, `sync.shared`, HTTP/2, chunked encoding, keepalive, `https`, `http.post` / header-custom `get`, `env.unset`, `cwd` / `chdir`, integer-to-string (use `net.addr`).

---

## 11. Ship-gates (after freeze and implementation — not this task)

### 0.17.1 (required later)

- Previous 0.16 suite green on macOS arm64 and Linux x86-64
- §1: worker `io.read_line` traps `stdin from worker`; origin thread still reads. `start` of a callee graph that contains those calls is a compile error. `env.set` in a worker is seen by the next `env.get` on any thread. Two concurrent `say`s do not byte-interleave
- `start` still cannot take `ref`. Unused `task` still a compile error. Planned-parallel `repeat` still rejects `start`
- 1000-message channel ping-pong (§2.8) prints `1` … `1000`
- `sync.recv` after `sync.close` and drain is `fail("closed")`. Drop of last end without close is implicit close
- `net.listen` + `net.connect` + `net.write` / `net.read` on localhost. Close then read is `fail("closed")`
- `http.get` against `http.serve_on` on localhost returns status `200` and the handler body
- Process exit still `yukt_task_shutdown` then `yukt_runtime_release`. Caps unchanged

### Mutex (later, after a share story or as a same-thread-only API)

- `lock` / `unlock` / move-while-locked / unlock-unlocked trap as §3
- Not required to coordinate two tasks until a later design adds a share type

---

## 12. Decisions (review checklist)

| Id | Decision |
| --- | --- |
| D-IO | `io.program` / `args` any thread, owned copies; stdin origin-thread only (compile + trap); `env.*` one internal lock; `say` one print lock |
| D-CH | Two ends via `channel` + `receiver`; no tuples; `recv` after close is `result[E]` / `fail("closed")`; drop = implicit close; unbounded |
| D-MU | Mutex API specified; **deferred**; no `sync.shared` |
| D-NET | POSIX TCP only; owned `conn` / `listener`; `fail("io")` / `fail("closed")`; `net.addr` for bound `host:port` |
| D-HTTP | `http.get` → `result[Response]`; `Request` / `Response` records; handler is a named `create` (no param type annotation); `serve` + `serve_on`; `http.serve` bind traps; sequential accept; `Content-Length` override; handler is not a stdin edge of the containing function; no TLS |
| D-OWN | No share type. Queue/fd internals are not YUKT values |
| D-ERR | `result` for recoverable close/I/O; traps for programmer bugs |
| D-RT | Same pool; caps frozen; shutdown then release; ends/listeners/conns tracked |

---

## Approval

**Accepted.** Human freeze 2026-11-01. Language law is `docs/LANGUAGE_SPEC.md` § 0.17 and `docs/STDLIB_SPEC.md`. This file remains the rationale. A follow-on implementation plan is a later task. Do not implement 0.17 until that plan exists.
