# YUKT 0.13 design — Systems language foundations

Status: **roadmap and this design are approved.** 0.13.1 Records is implemented and tagged `v0.13.1` locally (not pushed). `v0.12.1` is the last public tag. 0.13.2 Ownership is copied into `LANGUAGE_SPEC.md` and in progress on branch `yukt-0.13.2-ownership`. Do **not** start 0.13.3. Do **not** copy 0.13.3–0.13.6 into `LANGUAGE_SPEC.md` yet.

0.13 is a **language** milestone. Maps, files, and JSON stay blocked until types, ownership, and errors exist as real language rules. They must not appear as C functions with fake YUKT wrappers.

Each slice below is **independently releasable**. Shipping 0.13.1 must not require 0.13.2 to be correct. A later slice may extend a type; it must not silently change the meaning of programs that only use earlier slices.

## Goal

A small, readable language that compiles to host-native code, with transparent performance and predictable memory. Same path as 0.12: handwritten C++20 frontend, LLVM backend, no interpreter, no GC, no package manager, no cross-compilation.

## What 0.12 already froze (do not weaken)

- Indentation syntax, `create` / `say` / `give` / `if` / `repeat`
- Types: integer, float, boolean, string, homogeneous list
- No implicit conversion
- Signed overflow undefined; float reduction association may change
- Allocator **caps** in `docs/RUNTIME.md` (raise them only with a new documented contract)
- Module DAG, `use` / `share` / `as`, one native executable
- Traps for programmer bugs (bad string/list slice bounds)
- Frontend stays LLVM-free

The 0.12 **process-exit tracker** is not an ownership model. 0.13.2 must not pretend it is.

## Slice roadmap

| Slice | Ships | Must not require |
| --- | --- | --- |
| **0.13.1 Records** | Named product types of **scalars and nested records**, field access, constructors, layout, visibility — **implemented** | Heap fields, drop, `ref`, `foreign`, `result` |
| **0.13.2 Ownership** | One owner per heap allocation; move; overwrite release; nested heap fields | Borrow checker, FFI, errors |
| **0.13.3 References** | `ref` parameters, static borrow rules | FFI, errors |
| **0.13.4 Foreign C** | Typed `foreign` declarations, ABI, link set, safety boundary | Error values |
| **0.13.5 Errors** | Tagged `result[T]`, `ok` / `fail`, handling and propagation | Maps, files, JSON |
| **0.13.6 Stdlib** | `std/map`, `std/set`, `std/file`, `std/json`; `std/array` only if it is a distinct type | — |

`std/array` is not an alias of `std/list`.

## Cross-cutting rule: records ≠ ownership ≠ tracker

1. **0.13.1 defines records as values with a C-compatible layout.** Allowed field types are only `integer`, `float`, `boolean`, and other records (DAG). No `string`, `list`, map, or `ref` fields in 0.13.1. Copying a record copies bits. There is nothing to free.
2. **0.13.2 adds heap-typed fields and the ownership state machine.** Layout of scalar fields does not change. Heap fields are extra slots that hold owners, not a reason to reopen padding rules.
3. **Runtime allocation tracking stays a separate mechanism** from language ownership. See [Runtime vs ownership](#runtime-vs-ownership).

## Syntax collisions (locked)

`.` already means **module qualification** on callees: `math.add(1, 2)` requires `(...)`.

0.13.1 adds **field access** on values: `point.x` has no call parentheses. Resolution uses the **symbol’s actual kind and type**, not spelling:

1. If the left name is a `use` alias in this file, it is a **namespace**. `alias.field` is an error (`math` is a module, not a value). `alias.name(...)` looks up a **shared** function. Unknown or private names error at `.` with a note of what is shared (0.12 rule).
2. Otherwise the left side is an expression. If its type is a record, the right name is a **field**. `point.add(1)` is an error (no methods in 0.13). Missing field names the type and the known fields.
3. If the left side is a known non-record value, error: `` `.` is field access or a module call, not valid on integer ``.
4. If the left name is unknown, keep the 0.12 `Unknown name` diagnostic. Do not guess “maybe a module.”

Field assignment: `point.x = 3`. No methods (`point.greet()` waits).

Proposed reserved words, added only when that slice’s spec lands in `LANGUAGE_SPEC.md`:

| Slice | New reserved words |
| --- | --- |
| 0.13.1 | `record` |
| 0.13.3 | `ref` |
| 0.13.4 | `foreign` |
| 0.13.5 | `ok`, `fail` |

Do not reuse `create` for types. `share` already exists and applies to records in 0.13.1.

---

## 0.13.1 — Records

Independently releasable. No heap fields. No destructors.

### Visibility

Same model as functions.

```yukt
record Point:
    x: integer
    y: integer

share record Size:
    w: integer
    h: integer
```

- `record` is **private** to the source file.
- `share record` is **exported**. Importers name it `alias.Size`.
- `share` is only valid immediately before `record` (or `create`, as today).
- A **shared function** must not mention a private record in a parameter type or `give` type (inferred or written). That would leak a private type across the module boundary.
- Construction of an imported record is `geom.Point(1, 2)`, not unqualified `Point`, unless this file also defines `Point`.
- A **private** record cannot be constructed, named, or used as a field type from another module, even if its fields are otherwise ordinary types. It also cannot appear in a `share create` signature or in a `share record` field list.

### Shape

```yukt
record Point:
    x: integer
    y: integer

create main:
    p = Point(1, 2)
    say p.x
    p.y = p.y + 1
```

- Field types are written. Function bodies still infer.
- Allowed field types in **0.13.1**: `integer`, `float`, `boolean`, nested record types that are in scope.
- **No recursive records**, including indirect cycles. The record-type graph is a DAG. `record Node: next: Node` is an error. `record A: b: B` plus `record B: a: A` is an error, reported on the edge that closes the cycle.
- Field types may name a record declared **later in the same file**. Collect every record declaration, resolve field types, then validate the DAG and compute layouts. Source order is not a restriction.
- No inheritance, methods, generics, default field values, or packed layout.

### Construction

Positional only, **declaration order**.

- `Point(1, 2)` is valid.
- Missing fields, extra fields, or a type mismatch: compile error at the constructor.
- No named-field construction (`Point(x: 1)`) in 0.13.
- Zero-field records are an error (no empty product yet).

### Layout (host ABI)

Field order is declaration order. Layout matches a C struct of the corresponding types on the **host** triple (the only triple 0.12/0.13 compile). Not packed.

| YUKT field | C stand-in | Size | Align |
| --- | --- | --- | --- |
| `integer` | `int64_t` | 8 | 8 |
| `float` | `double` | 8 | 8 |
| `boolean` | `int8_t` 0 or 1 (normalized on construction and field assignment) | 1 | 1 |
| nested record | that record’s layout | its size | its align |

Offsets:

1. `offset_0 = 0`
2. `offset_i = align_up(offset_{i-1} + size_{i-1}, align_i)`
3. Record alignment = max of field alignments (at least 1)
4. Record size = `align_up(end of last field, record alignment)`

`align_up(n, a) = (n + a - 1) & ~(a - 1)` for power-of-two `a`.

This is the layout 0.13.4 will pass to C for scalar records. Changing it later is an ABI break; do not “optimize” padding in the compiler.

Copy is memcpy of `size` bytes. Two records of the same type compare with `==` / `!=` **fieldwise** using existing per-type equality (boolean, integer, float). Padding is not compared.

- `Point(1, 2) == Point(1, 2)` → true.
- Float fields follow language IEEE equality, including `NaN != NaN`, so a record containing NaN is not equal to itself.
- Records of **different types** cannot be compared (`Point == Size` is a type error, not false).

Boolean fields are normalized to `0` or `1` on construction and assignment so the C ABI is stable. SSA `i1` in the 0.12 LLVM lowering is a mapping detail, not a second boolean type.

### Diagnostics

Unknown field, module used as a value, value used as a module, constructor arity/type errors, cycle in record types, private record escaping through `share create`.

### Regression gate (0.13.1)

- Entire 0.12 CTest suite green, including `tests/stdlib/`.
- New tests: private vs `share record`, import `alias.Point(...)`, field get/set, constructor arity/types, cycle rejection, `.` on integer, `.` on a module without `(...)`, `==` of two points.
- `file` of a native binary still matches the host architecture.
- No change to allocator caps, 0.11 benches, or `v0.12.1` behavior of programs that contain no records.

---

## 0.13.2 — Ownership

Independently releasable **on top of 0.13.1**. Programs that only use scalar records keep 0.13.1 meaning (bitwise copy, no drop).

This slice is the memory model. Implement it only after the state machine below is copied into `LANGUAGE_SPEC.md`.

### What is owned

A **heap allocation** is a string, a list, or (later) a map/set/file handle. Each allocation has **exactly one owner** at a time.

Integers, floats, booleans, and 0.13.1-style records of only those types are **plain values**. Assignment copies bits. They are not in the ownership state machine.

A record that **contains** an owned field is an owner of those fields. Moving the record moves every owned field together.

### Per-name states (heap-typed names only)

Tracked for each local, parameter, and record field whose type contains ownership.

| State | May read | May overwrite | May move out | May borrow |
| --- | --- | --- | --- | --- |
| **Uninit** | no | yes (first assign) | no | no |
| **Owned** | yes | yes (drops previous) | yes | yes |
| **Moved** | no | no | no | no |
| **Borrowed** | no (except through the live `ref`) | no | no | no |

`Uninit` is the 0.12 “assigned” flag for heap names. `Moved` is new: the name still exists, but the allocation does not.

**Moved-from is dead.** You cannot read it, index it, pass it, `give` it, assign through it (`moved[0] = 1`), or move it again. You also cannot **reassign** it in 0.13.2 (`s = zeros(3)` after move is an error). Re-initialization would be a later, explicit spec. Forbidding it keeps the state machine small.

### Transitions

```
Uninit --assign heap value--> Owned

Owned  --x = y (y heap)-->  x drops old if any; y becomes Moved; x becomes Owned
Owned  --give x-->          x becomes Moved; caller receives Owned
Owned  --overwrite x = e--> drop(x); then assign e
Owned  --end of function--> drop(x) unless this is `give` of x
Owned  --borrow for call--> Borrowed until that call (and nested calls that receive the ref) return; then Owned again

Moved  --any use-->         compile error (use after move)
Borrowed --move or drop-->  compile error (cannot move or drop while borrowed)
```

`give` **transfers**. The callee must not drop the given allocation. The caller owns it.

Passing an owned heap value **by value** into a function is a move into that parameter (the callee owns it and drops it unless it `give`s it back).

### Nested records

```yukt
record Profile:
    name: string
    scores: list
```

Allowed only in **0.13.2+**.

- Moving `profile` moves `name` and `scores` together. One drop of `profile` drops both allocations. Never drop fields separately after a whole-record move.
- **No field move-out.** `s = profile.scores` is an error if it would steal the list. Explicit copy remains `list.copy` once a borrow or other access exists; 0.13.2 may allow `list.copy` on a **borrowed** list (see 0.13.3) or require copy before the ownership slice grows. Until 0.13.3, `list.copy(profile.scores)` is a **read** of an owned field: treat it as a temporary borrow of that field for the call, then the record stays Owned.
- `profile.scores = zeros(n)` overwrites: drop the previous list, move the new one in.
- Partial-move records (some fields Moved, some Owned) **do not exist** in 0.13.2.

Double-free is a compiler bug. Use-after-free is a compiler bug. Tests must include move of `Profile`, overwrite of one field, and rejection of use after move of the whole record.

### What 0.13.2 does not do

- No block-scoped locals. Bindings stay function-scoped, as in 0.12.
- No `free` in user code. No destructor syntax.
- No GC.
- No borrow checker (that is 0.13.3). Temporary field reads for `say`, `count`, and passing to a function that takes an owned value still need a rule: **passing owned into a callee that takes `list` (by value) moves.** `count(items)` does not take ownership (builtin, like today). 0.13.2 must list each builtin as **borrow** (`count`, `say`, index read) or **move** (user function parameter of heap type).

Builtin classification (0.13.2):

| Form | Effect on owner |
| --- | --- |
| `say x`, `count(x)`, `x[i]`, `x[i] = v` | borrow for the statement/expression; owner stays Owned |
| `zeros`, literals, `str.*` that allocate | produce a fresh Owned value |
| `list.copy(x)` | borrows `x`, returns a new Owned copy |
| user `create f(items):` with heap param | **move** into `f` |

### Runtime vs ownership

These are different jobs.

| Mechanism | Responsibility |
| --- | --- |
| **Language ownership** | Who may use the value; when the **compiler** must insert a release |
| **Runtime `yukt_release(ptr)`** | Unregister this allocation, then size-class-put or `free`. Idempotent **must not** be required; double `yukt_release` of a live object is a compiler bug and may trap |
| **Runtime tracker** | Safety net: `yukt_runtime_release` at `yukt_main` return still walks anything **still registered**. Correct 0.13.2 programs should have already released or transferred everything they own. Leaks at process exit remain defined as “still tracked,” not as a second owner |
| **Size-class cache** | Unchanged caps, not thread-safe, reuse after release |

The tracker must distinguish:

1. **Registered, owned by some frame** (compiler has not released yet)
2. **Unregistered because transferred** (pointer handed to caller; callee must not release)
3. **Released** (unregistered; payload cached or freed)
4. **Cached idle** (size-class slot; not a live YUKT value)

Move is (1) → still (1) but on another name: **no runtime call**, only compiler state. Drop is (1) → (3) then maybe (4). Transfer on `give` is compiler state plus “do not drop in this frame.”

Do not implement ownership by leaving everything on the process-exit list and hoping.

### Regression gate (0.13.2)

- 0.12 and 0.13.1 tests green.
- Use-after-move rejected.
- `give` of a list does not free before the caller reads it.
- Overwrite drops the previous allocation (observable with a later cache-reuse test or a leak tracker count).
- Nested `Profile` move does not double-free (ASan/UBSan or an explicit runtime poison on debug builds).
- Allocator cap unchanged.
- Parallel `repeat` still refuses bodies that allocate.

---

## 0.13.3 — References

Independently releasable. 0.13.1/0.13.2 programs without `ref` stay valid.

```yukt
create increment(value: ref integer):
    value = value + 1
```

`ref T` is a **borrow** of an existing place. It is not nullable, not a C pointer, and not a YUKT value you can store.

### Alias and lifetime rules (simple on purpose)

This is not Rust. One place, one story.

1. **Places.** A place is a local, a parameter, or a whole record value. In 0.13.3, **any field borrow counts as a borrow of the whole record**. You cannot pass `ref person.age` and `ref person.height` to the same call (or overlapping live calls). Disjoint-field borrowing waits.
2. **One mutable borrow at a time.** A place in `Borrowed` (from 0.13.2) has a single live mutable `ref`.
3. **No mixed borrows.** While a mutable `ref` is live, there is no second `ref` (mutable or not) to that place. Multiple **immutable** borrows are allowed only if 0.13.3 introduces a read-only `ref` spelling. **0.13.3 ships mutable `ref` only.** Immutable borrows wait; reads during a `ref` call go through that one `ref`.
4. **Cannot store.** A `ref` cannot be a record field, a list element, or a local you assign into. It exists only as a parameter.
5. **Cannot escape.** `give` of a `ref` is an error. Returning a borrow as an owned value is an error.
6. **May pass along.** A function may pass its `ref` parameter to a callee that also takes `ref T`. The original place stays `Borrowed` until the **outermost** call that created the borrow returns.
7. **Static.** All of the above are compile-time. No runtime borrow flag on objects.

Call site: passing `x` to `ref integer` borrows `x` for the call. The caller writes `increment(x)`, not `increment(&x)`. No `*` and no `&` in YUKT source.

Heap `ref list` / `ref string` (if allowed in 0.13.3) borrow the owner: the owner cannot move until the call returns. A `ref` is never an owning pointer.

### Regression gate (0.13.3)

- Previous slices green.
- Two overlapping `ref` to the same local rejected.
- Two field refs of one record in one call rejected.
- `give` of a `ref` rejected.
- Nested `increment(increment)`-style pass-along accepted.
- Storing `ref` in a record rejected.

---

## 0.13.4 — Foreign C

Independently releasable. Ordinary YUKT still has **no raw pointers**.

### Declaration

```yukt
foreign strlen(s: ref string): integer
```

- No body. Explicit parameter and return types.
- The symbol name is the C identifier (`strlen`).
- `foreign` is not `share`. Another file calls a `share create` wrapper in this file if it must.

### What may cross the boundary in 0.13.4

| YUKT | C | Ownership |
| --- | --- | --- |
| `integer` | `int64_t` | copy |
| `float` | `double` | copy |
| `boolean` | `int8_t` | copy |
| `ref string` | `const char*` | **borrow**; NUL-terminated YUKT buffer |
| `ref Record` | pointer to the 0.13.1 layout | **borrow** of a scalar/nested-scalar record |

Not in 0.13.4: owned `string` parameters, returned pointers, `list`, maps, `ref` to heap records with owned fields, `void*`, nullable pointers.

### Contracts the compiler assumes (and C must honor)

For `ref string` and `ref Record`:

- Callee **must not** store the pointer after return.
- Callee **must not** `free` it.
- Callee **must not** mutate it (`const`).
- Callee **must not** return that pointer as a result.
- The pointer is never null: YUKT does not pass a null C string (empty string is `""`).

If a C function can retain, mutate, return null, or take ownership, it is **out of scope** until a later slice adds explicit markers. Do not encode those facts as comments in wrappers.

Returned `integer` / `float` / `boolean` only. A C `char*` return is not a YUKT `string` in 0.13.4 (no silent copy, no silent leak).

### Linking

- Still `clang` + `libyukt_runtime.a` + `-lm` as today.
- 0.13.4 foreign symbols resolve against the **same** link line (libc, libm, libSystem on macOS). No `dlopen`, no per-file extra `-lfoo` until a later spec names how `yukt build` receives libraries.
- Missing symbols are link errors, not runtime `NULL`.

### Regression gate (0.13.4)

- Previous slices green.
- `foreign strlen` on a YUKT string matches C `strlen` for empty and non-empty.
- Type errors if you pass an owned string where `ref string` is required without a borrow (call site borrows automatically if the place is Owned; passing `ref` is the parameter type, the argument is still the name).
- No pointer type in `--types` for ordinary functions.

---

## 0.13.5 — Errors

Independently releasable. Traps stay for programmer bugs. I/O and parse failures are **values**.

**Do not implement untagged unions. Do not implement errors as ad-hoc runtime conventions.**

### Type

A function either returns a **plain type** `T` (today’s `give`) or a **tagged result** `result[T]`.

One function, one return type. Mixing `give 1` and `give fail(...)` in the same function is a type error unless the function’s type is `result[integer]`.

`result[T]` has two arms:

| Tag | Payload |
| --- | --- |
| `ok` | a value of type `T` |
| `fail` | a value of type `error` |

`error` is a **language type**, not a stdlib wrapper around `int`. 0.13.5 payload is **only a code string**:

```
error.code : string
```

No integers, no nested errors, no structured payloads in this slice. `fail("not_found")` builds that error.

Layout (for the compiler, not for user C): a host struct `{ int8_t tag; /* 0 fail, 1 ok */; error fail; T ok; }` with padding according to the same align rules as records. Only one payload is live. User code cannot take a pointer to the inactive arm.

### Constructors

Builtins (reserved words once this slice lands):

- `ok(x)` has type `result[T]` if `x` has type `T`
- `fail(code)` has type `result[T]` for a `T` fixed by the function’s other `give`s / signature

A function that never fails still uses plain `give x`. Do not wrap every function in `result`.

### Access and handling

A `result[T]` is **not** a `T`. Using it as `T` is a type error.

```yukt
r = open(path)
if r.ok:
    data = r.value
    say data
otherwise:
    say r.error.code
```

- `r.ok` is boolean (`true` iff tag is `ok`).
- `r.value` is valid only on paths where `r.ok` is known true (**flow typing** in `if r.ok` / `otherwise`). Elsewhere: compile error.
- `r.error` is valid only on paths where `r.ok` is known false.
- There is no implicit unwrap.

### Propagation

```yukt
create wrapped(path):
    r = open(path)
    if r.ok:
        give ok(r.value)
    otherwise:
        give r
```

`give r` is allowed when `r` has type `result[T]` and the function returns `result[T]`. The error is **the same value** (same code string owner moved). No conversion.

There is no `?` operator in 0.13.5.

### Unhandled errors

If a `result[T]` is created and **not** inspected (`if r.ok`), **not** given, and **not** assigned to a name that is later handled, that is a **compile error** (`unused result`). Discarding with `_` does not exist. You cannot `say` a result (type error).

No runtime “panic if you forget.” Forgetting is a type error.

### `std/error`

May exist after this slice as helpers (`error.code` is already a field). It must not invent a second error type.

### Regression gate (0.13.5)

- Previous slices green.
- `result` vs `T` mismatch errors.
- Flow typing: `r.value` outside `if r.ok` rejected.
- Unused result rejected.
- `give r` propagates the same code string.
- Traps (bad slice) still abort; they do not become `fail`.

---

## 0.13.6 — Stdlib expansion

Only after 0.13.1–0.13.5.

| Module | Needs from this design |
| --- | --- |
| `std/map` | opaque owned heap type; string keys first; `get` uses `result` or `has` plus a trap — **language spec chooses `result` in 0.13.5**, so `map.get` should be `result[V]`, not a sentinel |
| `std/set` | same as map |
| `std/file` | `foreign` + `result`; missing file is `fail`, never `""` |
| `std/json` | maps + a JSON value type + `result` |
| `std/array` | only with a distinct type and layout story |

Maps may allocate on `set`. Not thread-safe. Not in parallel `repeat` bodies. No list `append`.

---

## Compiler architecture

| Slice | Frontend | Runtime |
| --- | --- | --- |
| 0.13.1 | Parser: `record` / `share record`; `.` field vs module; constructors. Semantic: layouts, DAG, visibility. IR/LLVM: struct load/store, memcpy | Unchanged tracker |
| 0.13.2 | Move checker; insert `yukt_release` / no-op on transfer | New `yukt_release`; tracker unregisters; cache caps unchanged |
| 0.13.3 | Borrow checker (rules above) | Unchanged |
| 0.13.4 | `foreign` decls; call lowering | Link libc as today |
| 0.13.5 | `result[T]`, flow typing, unused-result | Optional helpers only if lowering needs them |

LLVM stays in `compiler/backend`. No benchmark-named special cases. Sort stays `std/sort`.

## Compatibility gate (every slice)

1. Full 0.12 CTest, including stdlib programs.
2. All tests of **previous 0.13 slices**.
3. Native `file` still reports a host executable.
4. Allocator cap 8,948,352 bytes unless a **new** runtime contract is documented first.
5. `benchmarks/results-0.11.json` not overwritten.
6. Host-native CI: macOS arm64 and Linux x86-64.

## Non-goals for 0.13

- Garbage collection
- Methods / impl blocks
- Generics beyond “one map has one key type and one value type”
- `and` / `or` / `not`
- Named-field constructors
- Recursive records
- Reassignment of moved-from names
- Disjoint field borrows
- Immutable `ref` (until a later spec)
- Returned C pointers / nullable pointers
- Extra `-l` flags beyond the current link line
- `?` error operator
- Package manager, cross-compilation, GPU

## What happens next

1. 0.13.1 records are specified and implemented.
2. 0.13.2 ownership is specified in `docs/LANGUAGE_SPEC.md` and in progress on branch `yukt-0.13.2-ownership`. Implement **only** 0.13.2. Stop.
3. Do not implement refs, FFI, or errors (0.13.3–0.13.6) until those sections are copied into the language spec as well.

## Approval

Roadmap: **accepted as the six independently releasable slices.**  
Revised design: **accepted.**  
0.13.1 in `LANGUAGE_SPEC.md`: **specified and implemented on this tree.** There is no public `v0.13.1` tag yet.  
0.13.2 ownership in `LANGUAGE_SPEC.md`: **specified; implementation in progress on branch `yukt-0.13.2-ownership`.**  
Compiler implementation of 0.13.3–0.13.6: **held.**
