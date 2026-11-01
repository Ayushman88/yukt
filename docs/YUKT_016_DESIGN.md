# YUKT 0.16 design — Growing collections

Status: **reviewed, frozen (2026-11-01), and implemented.** D1–D11 accepted with the clarifications below. Language guarantees live in `docs/LANGUAGE_SPEC.md` (§ 0.16.1). Later 0.16 slices (stdin, environment, richer strings) are still unspecified. No new reserved words. HTTP, mutexes, and channels remain 0.17.

**Read first:** `docs/LANGUAGE_SPEC.md` (lists, ownership, `ref`, `result`, `repeat`), `docs/STDLIB_SPEC.md`, `docs/RUNTIME.md`, `runtime/yukt_runtime.h` (`YuktList`), `compiler/backend/llvm_backend.cpp` (`list_data_pointer`, `invariant.load`), `std/list.yukt`.

A growing list is not an `append` function. It changes allocation, reallocation, `ref` validity, element lifetimes, LLVM payload assumptions, and what “index `i` still means” after a resize.

---

## 1. Motivation and scope

Before 0.16.1, a YUKT list was a heap header plus a payload whose **length is the allocation**. Lists did not grow. `items = []` could not be filled. Numeric kernels pre-size with `zeros(n)` or a literal. That is enough for matmul and not enough for a CLI that reads an unknown number of lines.

0.16 adds **one** list type that can grow and shrink in length, without a second “vector” type and without methods.

In scope:

- `list.append`, `list.remove`, `list.pop`
- length vs capacity
- automatic growth
- iteration and mutation during iteration
- ownership of plain values, strings, records, nested lists
- bounds, empty-list, allocation failure, integer overflow
- compatibility with every existing list program

Out of scope: `std/array`, insert-at-index, splice, `while`, for-each syntax, stdin, HTTP, sharing a list across `start` tasks.

---

## 2. Existing collection semantics (facts, not proposals)

These are locked today. 0.16 may change only the sentences this document marks as **replaced**.

### 2.1 Language

- A list is a homogeneous heap value `list[E]`. `E` is inferred. There is no `list[E]` spelling on locals.
- `count(items)` is the number of elements. `zeros(n)` allocates `list[integer]` of length `n`.
- `items[i]` / `items[i] = v`: index is `integer`, first slot is `0`.
- Integer and float index is **unchecked**. Out of range is **undefined**. Boolean, string, nested-list, and record-list index **traps**.
- `items = []` is a typed empty list only after later uses fix `E`; otherwise `Cannot infer type`. Before 0.16.1 it could not be filled.
- `repeat i in start..end` is inclusive. If `end < start`, the body does not run (`compiler/ir/lower.cpp` `too_small` branch). So `repeat i in 0..(count(xs) - 1)` on an empty list is already a no-op.
- Range bounds are lowered **once** before the loop. The trip count does not re-read `count` each iteration.

### 2.2 Ownership (0.13.2)

- A list header has exactly one owner.
- `items[i]` is a **borrow**. Binding a heap element out (`s = items[0]`) is `Cannot move out of list element`.
- `items[i] = v` drops the previous slot, then moves `v` in.
- `list.copy` / `list.slice` borrow the source and return a new owner; heap slots are cloned.
- `list.fill` / `list.reverse` / `sort.*` borrow the destination; they do not move the list.
- Passing a list to a user `create` **moves** it. `ref list` borrows it. `ref` cannot be stored. List **elements are not places**: `increment(xs[0])` is `Cannot borrow a temporary`.
- `start` cannot take `ref`. A list cannot be shared across tasks.

### 2.3 Runtime and LLVM (implementation today)

```c
typedef struct YuktList {
    int64_t length;
    int64_t kind;
    void* data;
    int64_t stride;
} YuktList;
```

- There is **no capacity**. `yukt_list_new` allocates `length * stride` (padded to 16 bytes, size-class cache). `length == 0` may have `data == NULL`.
- Header and payload are separate allocations. The header is what `yukt_track` registers as `YUKT_ALLOC_LIST`.
- Drop walks `0 .. length-1` for string/nested-list slots, then caches or frees the payload, then frees the header.
- LLVM `ListLen` loads field 0. `list_data_pointer` loads field 2 and marks it **`invariant.load`**, 16-byte aligned. Integer/float get/set are in-bounds GEP, not `yukt_list_get_i64`.
- OOM and bad index call `die` → `YUKT runtime error: …` and `exit(1)`. They do not become `fail`.
- One recursive heap mutex. A `repeat` whose body allocates stays sequential.

`invariant.load` on `data` is correct only while the payload pointer never changes. Growth makes that assumption false unless the compiler proves the region cannot append/remove/pop. Frozen implementation constraint: §10.4.

---

## 3. Growing-list model

### 3.1 One type

**[Language]** There is still one type, `list[E]`. `zeros(n)`, `[1, 2, 3]`, `[]`, `list.copy`, `list.slice`, and `str.split` all produce values of that type. Any of them may later `append` / `remove` / `pop`.

There is no frozen-list type and no `std/array` alias.

**[Implementation]** A list that never grows may still be compiled like today (GEP, optional invariant payload) if the compiler proves no structural mutation reaches that region.

### 3.2 Header identity, payload mobility

**[Language]** A list **value** is the header. Operations that change length do not produce a new list and do not move the owner. A `ref list` taken before `append` remains a valid borrow of the same list after `append`.

Logical slots `0 .. count-1` are stable **by index, not by address**. After growth or a remove-shift:

- `items[k]` for an index that still exists names the same element that had that index after the operation’s specified shift;
- there is no YUKT operation that exposes an element pointer, so user code cannot observe a payload address.

**[Implementation]** Growth may allocate a new payload, copy or memmove element bytes, return the old payload to the size-class cache or `free`, and write a new `data` pointer into the **same** header. The compiler must treat `data` as variant across any call that may reallocate.

### 3.3 Length

**[Language]** `count(items)` and `list.len(items)` remain the number of live elements. They do **not** become capacity. After a successful `append`, `count` is one larger. After a successful `remove` or `pop`, `count` is one smaller. `count` of `[]` and `zeros(0)` is `0`.

Existing folding of `count` on a list literal or `zeros(n)` is still valid **until** a structural mutation of that name. After `append` / `remove` / `pop`, a later `count` is not a constant.

### 3.4 Capacity (not a user value in this proposal)

**[Language]** Capacity is not a source-level integer. Programs do not read or write it. The observable contract is:

1. `append` succeeds or traps (OOM / overflow). The user does not reserve.
2. Amortized append is cheap; a single `append` may copy every live element.
3. Spare slots beyond `count`, if any, do not contain live owned values and are not readable as `items[i]`.

**[Implementation]** The runtime may store a capacity ≥ length on the header (recommended: new field **after** `stride`, so LLVM field numbers 0–3 stay). Growth policy is §7.

**D2 frozen:** no `list.cap` / `list.reserve`. Capacity is not a user value.

### 3.5 Empty lists become writable

**[Language — replaces “lists do not grow”]** `items = []` is a live list of length 0. After `E` is known, `list.append(items, value)` is valid.

```yukt
use std/list

create main:
    items = []
    list.append(items, 10)
    list.append(items, 20)
    say count(items)
    say items[0]
```

Prints `2` then `10`.

Inference: the first `append` / `remove` / `pop` / index / `fill` that mentions `items` fixes `E`. `items = []` then only `say count(items)` still cannot infer `E`.

### 3.6 Index safety boundary (D1 — frozen, prominent)

0.16 does **not** make all list indexing safe. Three distinct cases:

| Form | Out-of-range / empty | Notes |
| --- | --- | --- |
| `items[i]` / `items[i] = v` | **Unchanged from 0.14.1.** Integer and float: undefined. Boolean, string, nested-list, record-list: trap. | Existing numeric kernels depend on unchecked GEP. |
| `list.remove(items, i)` | **Trap** if `i < 0` or `i >= count(items)`, including `list[integer]`. | Never undefined. |
| `list.pop(items)` | **Trap** if `count(items) == 0`. | Never undefined. Distinct message from remove. |

Checked `items[i]` is a **future language decision**. It must not appear as a silent change to existing index semantics.

---

## 4. `append`, `remove`, `pop`

No new keywords. Surface is `std/list`, same pattern as `list.copy` / `list.fill`. Programs write `use std/list`. The compiler/runtime implement the bodies; `std/list.yukt` remains a now-gated stub or a thin wrapper.

**Decision D7:** builtins `append(items, v)` like `count`. This draft says **no** — keep the `list.` qualifier so `create append` in user code does not collide.

First argument is always a **borrow** of a list place (local, parameter, or `ref list`), never a move of the list. The list stays Owned.

### 4.1 `list.append(items, value)` → void

**[Language]**

- `value` must have type `E`.
- Plain `E` (`integer`, `float`, `boolean`, plain record): copy bits into a new last slot.
- Heap `E` (`string`, `list[T]`, record that contains ownership): **move** `value` into the new slot. The source name becomes Moved, same as `items[i] = value`.
- After return, `count(items)` is the old count plus one. The new element is at index `old_count`.
- Does not `give` the list. Return is void (same as `list.fill`).

```yukt
use std/list

create main:
    nums = zeros(0)
    list.append(nums, 1)
    list.append(nums, 2)
    say nums
```

Prints `[1, 2]`.

```yukt
use std/list

create main:
    names = []
    s = "hi"
    list.append(names, s)
    # s is Moved; reading s is Use after move
    say names[0]
```

```yukt
use std/list

create main:
    nums = [1, 2, 3]
    list.append(nums, 4)
    say nums
```

Prints `[1, 2, 3, 4]`. `zeros` / literals are not sealed.

**Evaluation order:** evaluate `items` (the place), then `value`, then mutate. If evaluating `value` fails, the list is unchanged.

**Self-append (frozen).** The value must not be the same list header as the destination.

```yukt
list.append(items, items)
```

- If `E` is not `list[…]`, this is a **type error**.
- If `E` is `list[…]`, this would move the destination into one of its own slots. **Compile error** (`Use after move`, or `Cannot move out of a ref` when the destination is `ref list`).
- `list.append(items, items[0])` when `E` is a heap type is `Cannot move out of list element` (existing rule).

The compiler rejects these. If a later hole reached the runtime, the implementation must trap **without** storing a pointer to the header inside its own payload and **without** changing `count`. Do not define a working self-append.

**D11 frozen:** return is void. No chaining.

### 4.2 `list.remove(items, index)` → `E`

**[Language]**

- `index` is `integer`.
- If `index < 0` or `index >= count(items)`, **trap** (`list index out of range`). Not `fail`. Not undefined, including for `list[integer]`.
- Removes `items[index]`. **Remaining elements shift left and keep their relative order:** old index `j` (`j > index`) becomes `j - 1`. Index `j < index` is unchanged. This is required, not optional.
- **Ownership transfer (D8, frozen).** The removed element becomes the result. The list no longer owns that element. For heap `E`, the result is Owned (the only legal steal). For plain `E`, the result is a bitwise copy; the hole is not an owner. Neighbors are not dropped and not cloned; they stay owned by the list.

```yukt
use std/list

create main:
    nums = [10, 20, 30]
    x = list.remove(nums, 1)
    say x
    say nums
```

Prints `20` then `[10, 30]`.

```yukt
use std/list

create main:
    names = ["a", "b"]
    s = list.remove(names, 0)
    say s
    say names
```

Prints `a` then `[b]`. This is legal; `s = names[0]` is still `Cannot move out of list element`.

As a statement, `list.remove(items, i)` drops the returned value (plain discard; heap `yukt_release`).

`map.remove` stays void and key-based. The names are qualified; they do not clash.

### 4.3 `list.pop(items)` → `E`

**[Language]** `list.pop(items)` is `list.remove(items, count(items) - 1)` except that an empty list traps with a distinct diagnostic:

| Situation | Title |
| --- | --- |
| `count(items) == 0` | `Cannot pop from an empty list` |
| (remove only) bad index | `list index out of range` |

```yukt
use std/list

create main:
    names = ["a", "b"]
    say list.pop(names)
    say names
```

Prints `b` then `[a]`. After `s = list.pop(names)`, `s` owns the removed element. `names` does not. Dropping `names` must not release `s`.

```yukt
use std/list

create main:
    items = []
    list.append(items, 1)
    say list.pop(items)
    # list.pop(items) here must trap
```

**D3 frozen:** empty pop and invalid remove **trap**. They are not `result[E]`.

### 4.4 What these ops are not

- Not `push` (name unused).
- Not `insert` at an arbitrary index (non-goal).
- Not in-place `+=` on lists.
- Not methods: `items.append(1)` stays a parse/type error (`.` is qualification or record field).

---

## 5. Iteration semantics

### 5.1 No new loop form

**[Language]** 0.16 does not add `for`, `while`, `break`, `continue`, or for-each. Iteration remains:

```yukt
repeat i in 0..(count(items) - 1):
    say items[i]
```

On an empty list this is `0..-1`, which does not run. There is no `while count(items) > 0`. To drain:

```yukt
n = count(items)
repeat i in 1..n:
    say list.pop(items)
```

Bounds `1..n` are computed once from the **pre-drain** length. That is required; a live `count` inside an inclusive `repeat` is the wrong tool.

**D4 frozen:** no new iteration syntax.

### 5.2 Structural mutation during iteration

**Definitions**

- **Slot mutation:** `items[i] = v` or a field write through `items[i].x`. Length unchanged.
- **Structural mutation:** `list.append`, `list.remove`, `list.pop` on that list (including through `ref list`).

**[Language]**

1. `repeat` range bounds are evaluated **once**. Later `append` does not extend the trip. Later `remove` / `pop` do not shrink it.
2. Slot mutation of `items[i]` during a `repeat` is allowed if `i` is in range **at the time of the write**.
3. Structural mutation of a list that is an **iteration subject** of an enclosing `repeat` is a **compile error**: `Cannot resize list during iteration`.

A list name `xs` is an iteration subject of a `repeat` when any of these hold in that `repeat` (including nested):

- the start or end expression mentions `xs` (typically `count(xs)`);
- the body indexes `xs` (`xs[…]`);
- the body passes `xs` as `ref list` to a call.

```yukt
use std/list

create main:
    xs = [1, 2, 3]
    repeat i in 0..(count(xs) - 1):
        list.append(xs, 0)
```

Rejected at `list.append`.

```yukt
use std/list

create grow(dest: ref list):
    list.append(dest, 0)

create main:
    xs = [1, 2, 3]
    repeat i in 0..(count(xs) - 1):
        grow(xs)
```

Rejected at `grow(xs)`: `xs` is an iteration subject and the callee takes `ref list`.

```yukt
create main:
    xs = [1, 2, 3]
    repeat i in 0..(count(xs) - 1):
        xs[i] = xs[i] + 1
    say xs
```

Accepted. Prints `[2, 3, 4]`.

4. **Header-level mutation detection (D10, frozen).** The subject is the **list header**, not the variable spelling used in the `repeat`. Any structural mutation of that header while an iteration of that header is in flight is illegal, including through another name, a `ref list`, or a callee.

YUKT assignment of a heap list is a **move**, not an alias:

```yukt
use std/list

create main:
    items = [1, 2, 3]
    alias = items
    repeat i in 0..(count(items) - 1):
        list.append(alias, 4)
```

`alias = items` leaves `items` Moved. `count(items)` is `Use after move`. That program is rejected before resize analysis. Two **Owned** names never refer to one header.

True aliases of one header are:

- a `ref list` parameter that borrows the iteration subject (the `grow(xs)` example above);
- any future compiler hole that reaches the same header from two paths.

**Static.** The analyzer tracks the header behind each list name and each live `ref list`. If a `repeat` has iteration subject header *H*, any `list.append` / `remove` / `pop` whose destination resolves to *H* — directly or by passing *H* as `ref list` — is `Cannot resize list during iteration`. Matching is by header, not by identifier text.

**Runtime.** The header holds a generation, not user-visible. Entering an index `repeat` whose subject is *H* snapshots *H*’s generation. Every structural mutation of *H* increments it. If a snapshot is live and the generation differs, trap `list changed during iteration` and do not finish the illegal mutation’s visible length change if the bump can be applied first. This backstop exists because static analysis can miss a path; it is not optional.

**[Implementation]** Index lowering for integer/float may stay unchecked only when the compiler proves that header is not structurally mutated in the region **and** `i` is in range.

### 5.3 Parallel `repeat`

**[Language]** A loop body that calls `list.append` / `list.remove` / `list.pop`, or calls a user function that may do so, is **not independent**. It must stay sequential. Same as today’s “allocating bodies stay sequential,” made explicit for resize.

Overlapping structural mutation from two parallel iterations is not a defined data-race story; the compiler must not emit `yukt_parallel_for` for those bodies.

---

## 6. Ownership and lifetime rules

Existing 0.13.2 / 0.13.3 rules stay, with these additions.

### 6.1 The list owner

| Form | Effect on the list |
| --- | --- |
| `list.append(xs, v)` | borrow `xs`; stays Owned |
| `list.remove(xs, i)` | borrow `xs`; stays Owned |
| `list.pop(xs)` | borrow `xs`; stays Owned |
| `count(xs)`, `xs[i]`, `say xs` | borrow (unchanged) |
| user `create f(xs):` | move (unchanged) |
| `f(xs)` where `xs: ref list` | borrow (unchanged) |

`ref list` remains valid across growth. The callee may `append` through the `ref`. The caller cannot move `xs` until the call returns.

### 6.2 Elements

| `E` | `append` | `remove` / `pop` | `xs[i] = v` | drop of `xs` |
| --- | --- | --- | --- | --- |
| plain (`integer`, `float`, `boolean`, plain record) | copy in | copy out | copy in | nothing |
| `string` | move in; source Moved | move out; result Owned | drop old string, move in | release each live slot |
| `list[T]` | move in | move out | drop old list, move in | drop each live nested list |
| record with ownership | move the record in | move the record out | drop old record fields, move in | drop each live record’s fields |

`s = xs[i]` for heap `E` remains **illegal**. Steal is only `remove` / `pop`.

Realloc **must not** drop elements. It only moves payload bytes (pointers to owned strings/lists stay the same pointers).

After `pop` / `remove`, the vacated logical slot is not an owner. Spare capacity must be zeroed or otherwise non-owning so a later drop of the list does not double-release.

### 6.3 Nested lists and records

```yukt
use std/list

record Profile:
    name: string
    scores: list

create main:
    people = []
    p = Profile("ann", [1, 2])
    list.append(people, p)
    # p is Moved
    q = list.pop(people)
    say q.name
```

`q` owns `name` and `scores`. `people` is empty. Dropping `people` must not release `q`’s fields.

```yukt
use std/list

create main:
    outer = []
    inner = [1]
    list.append(outer, inner)
    # inner is Moved
    say count(outer[0])
```

`outer[0]` is a borrow of the nested list. `list.append(outer[0], 2)` is **rejected**: `outer[0]` is not a place (`Cannot borrow a temporary`). To grow a nested list, pop or remove it, append, then append it back — or pass a `ref` to a local that owns that nested list.

That last sentence is intentional and harsh. **D9 frozen:** list elements are not places. No `ref` of `xs[i]` in 0.16. That keeps payload relocation simple.

### 6.4 `repeat` and moves

Existing rule: cannot move an outer heap name inside `repeat`. `list.pop(xs)` does **not** move `xs`; it moves an **element**. That is allowed when `xs` is not an iteration subject (§5.2). Draining with a pre-counted `repeat` is the intended pattern.

`list.append(xs, s)` inside `repeat` moves `s` each iteration. `s` must be loop-local and newly assigned each iteration (existing rule).

### 6.5 Tasks

`start` still moves heap arguments. Two tasks cannot append to the same list. No change.

---

## 7. Capacity and allocation behavior

### 7.1 Language guarantees

1. The user never names capacity.
2. `append` either makes `count` grow by one or traps. It does not return a partial list.
3. **Allocation failure leaves the original list valid.** If growth cannot allocate, the process traps (`out of memory` or `list too large`). The destination still has its old `count`, old elements, and the same owner. No element is dropped, duplicated, or left in a half-moved payload. The failed `append` does not change `count`.
4. A single `append` may take time linear in the old `count` (payload copy).
5. A sequence of `n` appends from empty, with no intervening shrink, is amortized O(1) per append. The language does **not** freeze the factor 2.
6. `remove` / `pop` need not return spare bytes to the OS. Length shrinks; capacity may stay.
7. Integer overflow of `count + 1` or of `count * stride` is not wrap. It traps (§8).
8. Semantics do not mention size-class bins, 1 MiB caps, or `aligned_alloc`. Those stay `docs/RUNTIME.md`.

### 7.2 Implementation sketch (not language)

Recommended runtime, replaceable without a language change:

- Store `capacity` on the header (field after `stride`).
- Invariant: `0 ≤ length ≤ capacity`, `capacity * stride` fits in the payload allocation, slots `length .. capacity-1` are non-owning.
- Empty / `[]` / `zeros(0)`: `length = 0`, `capacity = 0`, `data = NULL` (today).
- `zeros(n)` / literal of length `n`: `capacity = n` (exact). First `append` reallocates.
- When `append` and `length == capacity`: `new_cap = capacity == 0 ? 8 : capacity * 2`. If `new_cap` or `new_cap * stride` overflows `size_t` / `int64_t`, trap **without** touching the old payload. **Allocate the new payload first.** On allocation failure, leave `data`, `length`, and `capacity` unchanged, then trap. Only after the new payload is live: copy `length` elements, cache/free the old payload **without** walking elements, store new `data` and `capacity`, then write the new slot and increment `length`. Never free the old payload before the new one is installed.
- Update the tracker’s payload byte count for the header.
- Take `yukt_heap_mu` for the realloc path (allocator is not thread-safe).
- Do not shrink capacity on `pop` / `remove` in 0.16.

**D6 frozen:** doubling and the empty start of 8 are **implementation**, not language.

### 7.3 `list.copy` / `slice` / `zeros`

**[Language]** New lists have `count` equal to today’s meaning. They may be grown later.

**[Implementation]** Copy/slice should set `capacity == length` (no implicit spare). That keeps `list.copy` allocation size stable for existing tests that watch live allocs / payload classes.

---

## 8. Error semantics

YUKT already has two failure worlds. 0.16 must not mix them quietly.

| Class | Mechanism | Used for |
| --- | --- | --- |
| Trap | `YUKT runtime error: …` + `exit(1)` | bad index on checked lists, bad slice, OOM, double-wait |
| `result[T]` | `ok` / `fail` | missing map key, file, JSON |
| Undefined | no diagnostic | signed overflow; **today** integer/float list OOB |

### 8.1 0.16 operations (language)

| Situation | Effect | Message (proposed) |
| --- | --- | --- |
| `remove` index `< 0` or `≥ count` | trap | `list index out of range` |
| `pop` on empty | trap | `Cannot pop from an empty list` |
| `append` / `remove` / `pop` on a Moved list | compile | `Use after move` |
| `append` value type ≠ `E` | compile | `Type mismatch` |
| `append` that moves the destination into itself | compile | `Use after move` |
| structural mutation of an iteration subject | compile | `Cannot resize list during iteration` |
| unseen resize vs live index | trap | `list changed during iteration` |
| `count + 1` or byte size overflow | trap | `list too large` |
| allocator returns null | trap | `out of memory` |
| `zeros(-1)` | trap (already) | `negative list length` |

Allocation failure is **not** `fail("oom")`. A program cannot recover with `if r.ok`. Same as today’s `yukt_list_new`. After the trap path is entered from a failed grow, the destination list is still a valid owned list of its previous length (§7.1.3).

### 8.2 Index `items[i]` — D1 frozen

See §3.6. Compatibility compromise: `items[i]` keeps 0.14.1 out-of-bounds behavior. `remove` / `pop` always trap. A later checked-index design must be an explicit spec change, not a silent fix.

### 8.3 Diagnostics

Every compile error has file, line, column, what was found, why, and a fix, in the existing diagnostic style.

Runtime traps print `YUKT runtime error:` plus the title. They do not become `fail`.

---

## 9. Backward compatibility

### 9.1 Source that must keep its meaning

A program that never calls `list.append` / `list.remove` / `list.pop` must:

- parse and type-check as it does on 0.14.1;
- print the same `say` of lists (`[1, 2, 3]` with live `count` elements only);
- keep the same ownership errors (`Cannot move out of list element`, `Use after move`, …);
- keep `count` / `zeros` as compiler builtins;
- keep `list.copy` / `slice` / `fill` / `reverse` / `sort.*` contracts, except that the **result** is growable if the user later appends (observably identical until then).

`items = []` without a later `append` is still `Cannot infer type` if `E` is unknown.

### 9.2 What existing programs gain without writing new calls

Nothing observable, if they do not grow. `zeros(512)` is still length 512. Index loops still see 512 slots.

### 9.3 ABI / compile change (not source)

Adding `capacity` to `YuktList` and dropping blanket `invariant.load` can change generated code for **all** list programs, including those that never grow. That is an implementation risk, not a source break.

**[Implementation requirement]** Field order must stay `length, kind, data, stride, [capacity]`. LLVM GEP 0 and 2 keep meaning. Sequential matmul Experiment 1 is the honesty check if someone later measures; do not overwrite frozen JSON.

### 9.4 Spec files

Accepted language guarantees are in `docs/LANGUAGE_SPEC.md` § 0.16.1 and `docs/STDLIB_SPEC.md` (`list.append` / `remove` / `pop`). Those files replace “lists do not grow” for 0.16. The compiler implements the calls.

---

## 10. Runtime and compiler implications

Do not implement these in this task. They are the work a later plan will schedule.

### 10.1 Runtime (`runtime/yukt_runtime.c`, `.h`, `docs/RUNTIME.md`)

- Add `capacity` (or equivalent) on `YuktList`.
- `yukt_list_append_*` / `remove_*` / `pop_*` by kind (i64, f64, bool, cstr, list, rec), or one function plus kind switch.
- Realloc path: new payload, copy, cache old payload, no element drop, tracker byte update, heap mutex.
- Generation counter if §5.2.4 is accepted.
- `yukt_list_new` / `new_rec` / `copy` / `slice` initialize `capacity = length`.
- Drop still walks `length`, not `capacity`.
- Messages in §8.1.

### 10.2 YUKT IR (`compiler/ir`)

- New ops, e.g. `ListAppend`, `ListRemove`, `ListPop`. Not LLVM names in the frontend.
- `count` after a structural op is not folded.
- `stmts_allocate` / independence: resize is allocation + non-independent.
- Iteration-subject analysis in semantic or lowering (semantic is the right place for the compile error).

### 10.3 Semantic analysis

- Bind `list.append` / `remove` / `pop` as stdlib, borrow first argument, move or copy the value, return void or `E`.
- Empty-list inference from `append`.
- `Cannot resize list during iteration`.
- Keep `Cannot move out of list element` for `s = xs[i]`.

### 10.4 LLVM backend (payload is not header-stable)

**Constraint (frozen):** A pointer derived from a list payload must not be treated as invariant across a region in which that list may be structurally mutated. Any optimization relying on payload stability must prove the list cannot be resized during that region.

The header remains stable (`ref list` and the owner stay valid). The payload pointer may change. Today’s `list_data_pointer` `invariant.load` is valid only inside a proven no-resize region.

- Declare the new runtime symbols.
- Reload `data` after any call that may reallocate that header.
- Integer/float GEP remains valid between reallocs of that header, not across them.
- Frontend still must not include LLVM headers.

### 10.5 `std/list.yukt` / `STDLIB_SPEC`

- Document the three calls (after LANGUAGE_SPEC).
- Stub or wrapper like `copy` / `fill` (`std/list.yukt` bodies are not executed today).

### 10.6 Auto-par and tasks

- Resize in a `repeat` body ⇒ sequential.
- No `ref` across `start` ⇒ no cross-task append.

### 10.7 Tests and benches

- New tests under `tests/` (see §11). Do not edit frozen bench JSON.
- Do not raise `kMinParallelWork` to “test” lists.

---

## 11. Acceptance tests

Specification-only: these are the tests implementation must add later. Names are proposed paths.

### 11.1 Sequential behavior

| Test | Expect |
| --- | --- |
| `tests/lists/append_from_empty.yukt` | `[]` + two appends; `say` and `count` |
| `tests/lists/append_after_zeros.yukt` | `zeros(2)` then append; `[0, 0, x]` |
| `tests/lists/append_after_literal.yukt` | `[1, 2]` then append `3` |
| `tests/lists/remove_middle.yukt` | `[10,20,30]` remove `1` → `20` and `[10,30]` |
| `tests/lists/remove_ends.yukt` | remove `0` and last index |
| `tests/lists/pop_order.yukt` | pop twice; LIFO |
| `tests/lists/drain_counted.yukt` | `n = count`; `repeat i in 1..n: pop` |
| `tests/lists/count_not_capacity.yukt` | after many appends, `count` is live length only |
| `tests/lists/remove_preserves_order.yukt` | `[10,20,30,40]` remove `1` → `[10,30,40]` |

### 11.2 Traps

| Test | Expect |
| --- | --- |
| `tests/lists/pop_empty_trap.yukt` | empty pop aborts; message `Cannot pop from an empty list` |
| `tests/lists/remove_oob_trap.yukt` | `-1` and `count` trap even on `list[integer]` |
| `tests/lists/remove_oob_empty.yukt` | remove `0` from `[]` traps |

### 11.3 Ownership and memory

| Test | Expect |
| --- | --- |
| `tests/ownership/append_moves_string.yukt` | `list.append(xs, s)` then read `s` → `Use after move` |
| `tests/ownership/pop_steals_string.yukt` | `s = list.pop(xs)` legal; `xs` no longer contains `s`; drop `xs` does not free `s` |
| `tests/ownership/index_still_not_steal.yukt` | `s = xs[0]` still `Cannot move out of list element` |
| `tests/ownership/append_profile.yukt` | append `Profile`; source Moved; pop returns owned fields |
| `tests/ownership/append_nested_list.yukt` | move inner list in; drop outer drops inner if not popped |
| `tests/ownership/pop_then_live_allocs.yukt` | `yukt_debug_live_allocs` after pop+drop matches one owner |
| `tests/ownership/remove_does_not_drop_neighbors.yukt` | remove middle string; other strings still printable |
| `tests/ownership/realloc_does_not_drop.yukt` | enough appends to force growth; all strings still owned; no double-free |
| `tests/ownership/ref_append.yukt` | `create push(dest: ref list, v): list.append(dest, v)` mutates caller |
| `tests/ownership/move_while_ref_append.yukt` | move owner during `ref` call rejected |
| `tests/ownership/self_append_rejected.yukt` | `list.append(items, items)` compile error |
| `tests/ownership/assign_is_move_not_alias.yukt` | `alias = items` then `count(items)` is `Use after move` |
| `tests/runtime/grow_oom_keeps_list.c` | failed realloc leaves length and elements unchanged (runtime hook) |

### 11.4 Iteration

| Test | Expect |
| --- | --- |
| `tests/lists/iter_empty.yukt` | `repeat i in 0..(count(xs)-1)` on `[]` is a no-op |
| `tests/lists/iter_slot_write.yukt` | increment every `xs[i]` |
| `tests/lists/iter_append_rejected.yukt` | append inside that `repeat` is a compile error |
| `tests/lists/iter_pop_rejected.yukt` | pop inside index `repeat` is a compile error |
| `tests/lists/iter_ref_grow_rejected.yukt` | `grow(xs)` with `ref list` inside the `repeat` is a compile error |
| `tests/lists/drain_not_iteration_subject.yukt` | counted `1..n` pop loop accepted |
| `tests/lists/iter_header_alias_ref.yukt` | `ref` to the same header as the `repeat` subject + append is compile error |

### 11.5 Types and inference

| Test | Expect |
| --- | --- |
| `tests/lists/empty_infer_append.yukt` | `[]` then `append` integer |
| `tests/lists/empty_mixed_append.yukt` | int then string → type error |
| `tests/lists/append_zeros_string.yukt` | `zeros(0)` is `list[integer]`; append string → type error |

### 11.6 Parallel / tasks

| Test | Expect |
| --- | --- |
| `tests/lists/append_stays_sequential.yukt` | `--explain` reports sequential for a `repeat` that appends |
| `tests/tasks/no_ref_append_across_start.yukt` | `start` with `ref list` still rejected |

### 11.7 Compatibility

| Test | Expect |
| --- | --- |
| Entire existing CTest suite | green |
| `tests/stdlib/list_basic.yukt` | unchanged output |
| `tests/ownership/list_steal.yukt` | still rejected |

No benchmark file is an acceptance test for 0.16.

---

## 12. Explicit non-goals

- New keywords, methods, `for` / `while` / for-each
- `list.insert`, `list.clear`, `list.resize`, `list.push`, `list.reserve`, `list.cap`
- `std/array`
- Recoverable OOM via `result`
- Sharing one list across `start` / `wait`
- Silently making integer/float `items[i]` trap (would need a new D1 = B spec)
- `ref` to a list element (D9)
- Shrinking capacity; custom allocators; growth factor in the language
- Stdin, env, extra `std/str`, HTTP
- Compiler or runtime implementation in this documentation step
- Performance claims

---

## Decisions (frozen 2026-11-01)

| ID | Decision |
| --- | --- |
| **D1** | `items[i]` keeps 0.14.1 OOB behavior. `remove` / `pop` trap. Checked indexing is a later explicit design. |
| **D2** | No public capacity or reserve. |
| **D3** | Empty pop and invalid remove trap. |
| **D4** | Index `repeat` only. |
| **D5** | One list type. |
| **D6** | Amortized O(1) append; growth factor is runtime. |
| **D7** | `list.append` / `list.remove` / `list.pop`. |
| **D8** | `remove` / `pop` return `E` with ownership transfer. Remaining elements shift left, order preserved. |
| **D9** | No `ref` of a list element in 0.16. |
| **D10** | Compile-time header analysis plus runtime generation on the header. |
| **D11** | `append` returns void. |

Clarifications frozen with the table: §3.6 index boundary; §4.1 self-append; §4.2 / §4.3 ownership and shift; §5.2 header-level aliases; §7.1.3 allocation failure; §10.4 payload non-invariance.

---

## Approval

D1–D11 are **accepted**. Language guarantees are in `docs/LANGUAGE_SPEC.md` § 0.16.1. Compiler, runtime, and §11 tests are implemented.
