# YUKT 0.13 design — Systems language foundations

Status: **design, not implemented.** `v0.12.1` is the last 0.12 tag. Do not start coding until this document plus the 0.13 sections of `docs/LANGUAGE_SPEC.md` are accepted.

0.13 is a **language** milestone. Maps, files, and JSON are blocked on types, errors, and ownership. They must not appear as extra C functions with fake YUKT wrappers.

## Goal

Make YUKT usable for serious systems programs on the same host-native path as 0.12: handwritten C++20 frontend, LLVM backend, no interpreter, no GC, no package manager, no cross-compilation.

## What 0.12 already froze (do not weaken)

- Indentation syntax, `create` / `say` / `give` / `if` / `repeat`
- Types: integer, float, boolean, string, homogeneous list
- No implicit conversion
- Signed overflow undefined; float reduction association may change
- Allocator caps in `docs/RUNTIME.md` (raise them only with a new documented contract)
- Module DAG, `use` / `share` / `as`, one native executable
- Traps for programmer bugs (bad string/list slice bounds)
- Frontend stays LLVM-free

## Order of work

Specify each slice in `docs/LANGUAGE_SPEC.md` before implementing it. Implement in this order so later slices have types to name:

| Slice | Language | Then stdlib |
| --- | --- | --- |
| 0.13.1 | `record` types, field read/write, type names in records | — |
| 0.13.2 | Move of heap values; free when the owning frame returns | optional `list.copy` stays |
| 0.13.3 | `ref` parameters (borrows) | — |
| 0.13.4 | `foreign` C functions with explicit types | thin libc wrappers only where specified |
| 0.13.5 | Error **values** (`fail` / `ok`) | `std/error` |
| 0.13.6 | — | `std/map`, `std/set`, `std/file`, `std/json` as specified in `docs/STDLIB_SPEC.md` |

`std/array` waits until records exist and we decide it is not a lie for heap lists. Do not alias `std/list`.

## Syntax collisions to lock now

`.` is already **module qualification** on callees: `math.add(1, 2)` requires `(...)`.

0.13 adds **field access** on values: `person.age` has no call parentheses. Semantic analysis distinguishes them:

- Left side is a `use` alias → must be `alias.name(...)`.
- Left side is a value of record type → field.
- Left side is a value of non-record type → error.

`person.age = 21` is field assignment. No methods in 0.13 (`person.greet()` waits). That keeps `.` from growing a third meaning.

New reserved words (0.13 only, after the spec lands): `record`, `foreign`, `ref`, `fail`. Do not reuse `create` for types.

## 0.13.1 Records

```yukt
record Person:
    name: string
    age: integer

create greet(person):
    say "Hello, " + person.name

create main:
    p = Person("Ada", 36)
    greet(p)
    p.age = p.age + 1
    say p.age
```

Rules:

- A record is a named product of existing 0.12 types plus other records (no cycles at first; optional later).
- Field types are written. Function bodies still infer.
- Construction is `Name(field0, field1, ...)` in field-declaration order. No keyword fields in 0.13.
- Records of scalars and nested records are **values**. Copying a record copies fields. Heap fields (string, list) follow 0.13.2.
- No inheritance, no methods, no generics.

Why records before maps: a map is an opaque heap object, but JSON objects and C structs need a named layout in the language.

## 0.13.2 Ownership (no GC)

0.12 releases tracked heap at `yukt_main` return. 0.13 adds **intra-function free of values that this frame owns** when the function returns (or when the last owner is overwritten).

Rules:

- Strings, lists, maps, and other heap objects are **owned**.
- `give` **moves** an owned value to the caller.
- `x = y` for an owned heap value **moves**. Using `y` after that is a compile error (`use after move`).
- Copies stay explicit (`list.copy`, `str` functions that already allocate).
- Record copies move owned fields unless the field is `ref` (0.13.3).
- The allocator remains not thread-safe. Allocating calls still refuse automatic parallel `repeat`.
- No destructor syntax. No `free()` in user YUKT. The compiler inserts release at the end of the owning function for anything not moved out.
- 0.13 does **not** add block-scoped locals. Assignments still bind in the enclosing function, as today. That limits how early memory returns, and is honest.

This unblocks `std/file.read` without leaking every buffer until process exit, without pretending we have a GC.

## 0.13.3 References

```yukt
create bump(age: ref integer):
    age = age + 1
```

- `ref T` is a borrow of an existing name. It is not a nullable pointer.
- A `ref` argument cannot outlive the call. Storing a `ref` in a heap object or returning it is an error in 0.13.
- Integer/float/bool `ref` is a pointer to the local. Heap `ref` is a borrow of the owner; the owner cannot move while the borrow is live (the call is the live range).
- No pointer arithmetic. No `*` / `&` in YUKT source.

Raw C pointers exist only inside `foreign` signatures.

## 0.13.4 Foreign C

```yukt
foreign strlen(s: string): integer
```

Rules:

- `foreign` declarations have explicit parameter and return types. They have no body.
- Linking is still `clang` + `libyukt_runtime.a` plus libraries the spec lists (libc, libm). No dlopen in 0.13.
- Strings at the boundary are YUKT tracked buffers (NUL-terminated bytes), not borrowed C literals the runtime does not own — unless the spec marks a parameter `ref` and the callee promises not to retain it.
- Integer/float/bool match the existing LLVM ABI (`i64`, `f64`, `i8` bool).
- Records passed to C must be specified per type (by value vs pointer). Default: `ref Record` is a pointer to a C-compatible layout; by-value records of scalars are allowed when the layout is documented.
- `foreign` cannot be `share`’d as a YUKT body. Other modules call a thin `share create` wrapper that calls the foreign function.

No general libffi. Each foreign symbol is a typed declaration.

## 0.13.5 Errors

Traps stay for programmer bugs (bad slice, empty `stat.min`). I/O and parse failures are **values**.

```yukt
create open(path):
    if missing:
        give fail("not_found")
    give ok(handle)
```

Rules:

- `fail(code)` builds an error. `code` is a string (byte string, like the rest of 0.12).
- `ok(value)` wraps a success.
- A function that has any `give fail(...)` may only be used through `ok` / an `if` form that the spec will name. Do not silently turn traps into errors.
- No exceptions, no `setjmp`, no `Result` syntax sugar beyond `ok` / `fail`.
- `std/error` exists only after these forms are in `docs/LANGUAGE_SPEC.md`. Until then `docs/STDLIB_SPEC.md` rows stay 0.13.

Exact bind syntax (`if ok(data) = open(path):`) is written into the language spec in the 0.13.5 spec pass, not invented in the compiler first.

## 0.13.6 Stdlib that 0.12 deferred

Only after the slices above:

| Module | Needs |
| --- | --- |
| `std/map` | opaque heap type, one key type + one value type, string keys first, `set`/`has`/`len`; `get` needs errors or `has`+trap |
| `std/set` | same as map, string elements first |
| `std/file` | `foreign` + errors; missing file is `fail`, not `""` |
| `std/json` | maps + homogeneous lists + errors; mixed JSON arrays need a JSON value type (record or opaque) |
| `std/array` | only if we add a distinct type; not a list alias |

Still no append/pop/resize. Maps may grow internally; that growth is specified as allocator use, not as list-length lies.

## Compiler architecture (unchanged split)

- Parser: `record`, `foreign`, `ref`, field `IDENT.IDENT` without `(`.
- Semantic: record layouts, move checking, borrow checking for `ref` (function-scoped, not full Rust).
- IR: record load/store, drop at function epilogue, foreign declarations.
- LLVM: still confined to `compiler/backend`.
- Runtime: drop uses the existing tracker; may `free` before `yukt_main` returns. Size-class caches stay. Not thread-safe.

No compiler special cases named after benchmarks. Sort stays in `std/sort`.

## Testing

Every 0.13 language form needs tests under `tests/`. Every new `std/` module needs `tests/stdlib/` programs that `use` it and run natively. Keep 0.12 tests green. Do not overwrite `benchmarks/results-0.11.json`.

## Non-goals for 0.13

- Garbage collection
- Methods / impl blocks
- Generics beyond “one map has one key type and one value type”
- `and` / `or` / `not`
- Package manager
- Cross-compilation
- GPU / CUDA (0.14)
- Relaxing parallel allocator thread-safety

## First implementation after this design is accepted

1. Write the 0.13.1 `record` section into `docs/LANGUAGE_SPEC.md`.
2. Implement records + field access + tests.
3. Stop. Do not skip to maps or files.

## Approval

This file is the design. Implementation starts only after it is accepted and 0.13.1 is copied into the language spec.
