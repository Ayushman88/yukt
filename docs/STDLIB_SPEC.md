# YUKT standard library API contract

Status: **now-gated modules implemented** (`std/str`, `std/list`, `std/sort`, `std/math`, `std/stat`). 0.13 modules remain specified only. **0.16.1** `list.append` / `remove` / `pop` are implemented. **0.16.2** `std/io` and `std/env` are implemented. **0.16.3** richer strings (`trim` / `replace` / `lower` / `upper` / `to_int` / `to_float`) are **implemented**. See `docs/STDLIB_IMPLEMENTATION_REPORT.md` and `docs/LANGUAGE_SPEC.md` § 0.16.1, § 0.16.2, and § 0.16.3.

This file is the source of truth for **stdlib APIs**. `docs/LANGUAGE_SPEC.md` remains the source of truth for syntax. If this document needs a new type, keyword, or operator, that change belongs in the language spec first.

0.11 remains tagged `v0.11.0`. 0.12 is the module system (`v0.12.0`). This contract does not disturb host-native compilation, numerical semantics, allocator caps, or `benchmarks/results-0.11.json`.

## Process

1. **API specification** (this document): syntax, signatures, types, errors, mutability, behavior, ship-gate.
2. **Implementation**: YUKT under `std/` wherever the language can express it; C runtime primitives only where it cannot. No source concatenation into the entry file. No package manager.
3. **Validation**: correctness, allocation behavior, edge cases, and (later) performance against native baselines. Do not overwrite 0.11 bench files.

Implementation of a section starts only when that section is tagged **now**.

## How to read a ship-gate

| Gate | Meaning |
| --- | --- |
| **now** | Expressible with 0.12 types and `use` / `share`. May still need a runtime primitive. |
| **0.13** | Blocked on language work: structs or an opaque heap type, ownership / intra-function free, FFI, or an error type. The signature here is the intended contract, not a promise that 0.12 can compile it. |

`.` in examples is **module qualification** (`str.len(s)`), never a method.

## Locked constraints (do not weaken in implementation)

- **No new syntax** in the library. Calls look like `alias.name(...)`. No `list.len`, no `s[i]` on strings, no type annotations, no generics.
- **Types in 0.12:** integer (`i64`), float (`f64`), boolean, string, list of one element type. No map, set, struct, error, or char type.
- **Lists grow only through specified calls.** `list.append` / `list.remove` / `list.pop` are 0.16.1 ([`docs/LANGUAGE_SPEC.md`](LANGUAGE_SPEC.md) § 0.16.1). Index assignment still replaces a slot and does not change `count`. There is no `resize`, `push`, `cap`, or `reserve`.
- **Strings are immutable byte strings.** `+` concatenates and allocates. There is no string index syntax. Indices in `std/str` are **byte offsets**, 0-based. Encoding is the compiler’s C string: NUL-terminated bytes. UTF-8 decoding is not part of this contract.
- **Allocator:** size-class caches in `docs/RUNTIME.md`. Not thread-safe. No intra-function free in 0.12. Heap lists and strings are released when `yukt_main` returns. Reuse after `yukt_runtime_release` is bounded (four slots per class, 8,948,352 bytes retained). Collections must use this model.
- **Forbidden promises:** garbage collection, silent resize without `list.append` / `remove` / `pop`, and safe concurrent allocation. A library function that allocates must not run inside a compiler-parallelized `repeat` (same rule as `zeros` / concat today). `list.append` is an explicit resize, not an implicit one.
- **Numeric lists:** out-of-range `items[i]` is **undefined**. Boolean, string, and nested-list indexes **trap**. Stdlib functions that index numeric lists inherit undefined-on-OOB unless the row says **trap**.
- **Signed integer overflow** is undefined. Float association/rounding may change with parallel chunking (`docs/LANGUAGE_SPEC.md`).
- **`count` and `zeros` stay compiler builtins.** Library wrappers may call them. A user `create count` still shadows the builtin in that file.

## Failure model

Until 0.13 specifies an error type in the language spec:

| Class | 0.12 behavior |
| --- | --- |
| Domain / bounds the row marks **trap** | Runtime abort with a diagnostic on stderr; process exits non-zero. Not an exception. |
| Domain the row marks **undefined** | Same as language UB (no wrap, no saturate). |
| Search miss | Sentinel integer `-1` or boolean `false`, never a trap. |
| IEEE float domain (`sqrt` of negative, `0.0/0.0`) | IEEE-754 result (`NaN` / `Inf`). Not a trap. |
| Integer division by zero | Undefined (language rule). |

**0.13** replaces traps for I/O and parse failures with an error value. Do not invent `Result` or `try` in 0.12.

## Module layout

| Path | Alias | Gate | Role |
| --- | --- | --- | --- |
| `std/str` | `str` | now | Byte-string helpers, including 0.16.3 trim / replace / case / parse |
| `std/list` | `list` | now | List helpers, including 0.16.1 `append` / `remove` / `pop` |
| `std/sort` | `sort` | now | In-place sort and binary search |
| `std/math` | `math` | now | Scalar math |
| `std/stat` | `stat` | now | Reductions on numeric lists |
| `std/io` | `io` | now | 0.16.2 process args and stdin |
| `std/env` | `env` | now | 0.16.2 environment |
| `std/array` | `array` | 0.13 | Distinct array type (stack or sized); 0.12 lists are not arrays |
| `std/map` | `map` | 0.13 | Associative map |
| `std/set` | `set` | 0.13 | Unique keys |
| `std/file` | `file` | 0.13 | Filesystem I/O |
| `std/json` | `json` | 0.13 | JSON parse / write |
| `std/error` | `error` | 0.13 | Error values (language spec first) |

```yukt
use std/str
use std/list
use std/sort
use std/math
use std/stat
```

Each file exports only `share create` names listed below. Helpers that are not shared stay private.

## `std/str` — now

Implementation: **runtime primitives** (YUKT cannot index or iterate string bytes). Thin `share create` wrappers are allowed if the backend already lowers the same names; otherwise the compiler binds these names like builtins when the module is loaded. Either way, user code only sees `str.name(...)`.

All functions that return a string **allocate** a new buffer (size-class string cache). They do not mutate the argument. They must not be used in a parallel `repeat` body.

Empty string `""` is a valid argument. A null C pointer is not a YUKT value; the runtime must not expose one.

`n` and indexes are integers. Negative `n` for `repeat` **traps**.

| Call | Gives | Alloc | Failure | Behavior |
| --- | --- | --- | --- | --- |
| `str.len(s)` | integer | no | — | Byte count, not including the NUL terminator. `str.len("")` is `0`. |
| `str.at(s, i)` | integer | no | **trap** if `i < 0` or `i >= str.len(s)` | Unsigned byte `0…255` as an integer. |
| `str.slice(s, start, end)` | string | yes | **trap** if `start < 0`, `end < start`, or `end > str.len(s)` | Bytes `[start, end)` (end exclusive). `str.slice(s, 0, str.len(s))` copies `s`. `str.slice(s, i, i)` is `""`. |
| `str.find(s, sub)` | integer | no | — | Lowest byte index of `sub` in `s`, or `-1`. `str.find(s, "")` is `0`. |
| `str.contains(s, sub)` | boolean | no | — | `str.find(s, sub) != -1`. |
| `str.starts(s, sub)` | boolean | no | — | True iff `s` begins with `sub`. `str.starts(s, "")` is true. |
| `str.ends(s, sub)` | boolean | no | — | True iff `s` ends with `sub`. `str.ends(s, "")` is true. |
| `str.repeat(s, n)` | string | yes | **trap** if `n < 0` | `n` copies of `s` concatenated. `n == 0` gives `""`. |
| `str.join(parts, sep)` | string | yes | **trap** if `parts` is not `list[string]` | Concatenate elements with `sep` between them. Empty list gives `""`. One element gives that element with no `sep`. |
| `str.split(s, sep)` | list[string] | yes | **trap** if `sep` is `""` | Split on non-overlapping occurrences of `sep`. Allocates a list whose length is `1 +` number of separators. `"a,b,"` split on `","` gives three strings, last empty. |
| `str.trim(s)` | string | yes | — | **0.16.3 implemented.** New string; drop leading and trailing ASCII whitespace (`0x20`, `0x09`, `0x0A`, `0x0B`, `0x0C`, `0x0D`). See `LANGUAGE_SPEC` § 0.16.3. |
| `str.trim_left(s)` | string | yes | — | **0.16.3 implemented.** Leading only. Same whitespace set. |
| `str.trim_right(s)` | string | yes | — | **0.16.3 implemented.** Trailing only. Same whitespace set. |
| `str.replace(s, old, new)` | string | yes | **trap** `empty replace pattern` if `old` is `""` | **0.16.3 implemented.** Every non-overlapping `old`, left to right. Empty `new` deletes matches. `str.replace("aaa", "aa", "b")` is `"ba"`. |
| `str.lower(s)` | string | yes | — | **0.16.3 implemented.** ASCII `A-Z` → `a-z`. Other bytes unchanged. |
| `str.upper(s)` | string | yes | — | **0.16.3 implemented.** ASCII `a-z` → `A-Z`. Other bytes unchanged. |
| `str.to_int(s)` | result[integer] | no | `fail("bad_int")` | **0.16.3 implemented.** Trim, optional ASCII sign, then digits. Whole remainder must be that literal. Overflow and junk fail. |
| `str.to_float(s)` | result[float] | no | `fail("bad_float")` | **0.16.3 implemented.** Trim, then C `strtod` in the C locale on the whole remainder. NaN/Inf text and overflow fail. |

`==` / `!=` on strings stay language operators. `+` stays language concat. Do not add `str.concat`; use `+`.

0.16.3 adds the eight rows above. They are **implemented**. No Unicode case folding, no UTF-8 decode, no regex, no interpolation, and no `s[i]`. `join` / `split` / `find` / `at` / `slice` are unchanged.

## `std/list` — now

Implementation: **YUKT** for integer and float lists that only read/write existing slots; **runtime** when a new list of a non-integer element type must be allocated (`zeros` only builds `list[integer]`).

`src` and `dest` are lists. Lengths come from `count`. `len` / `copy` / `fill` / `reverse` / `slice` do not change `count` except that `copy` / `slice` produce a new list.

| Call | Gives | Mutates | Alloc | Failure | Behavior |
| --- | --- | --- | --- | --- | --- |
| `list.len(items)` | integer | no | no | — | Same as `count(items)`. Not capacity. |
| `list.copy(src)` | list | no | yes | — | New list, same length and element type, elementwise copy. Numeric OOB does not apply (full length). The copy may later grow. |
| `list.fill(dest, value)` | void | dest | no | type mismatch is a compile error | Every slot becomes `value`. `value` must match the element type. Lists of records are a type mismatch; record fill is not implemented. Assign with `items[i] = value`. |
| `list.reverse(dest)` | void | dest | no | — | Reverse in place. |
| `list.slice(src, start, end)` | list | no | yes | **trap** if `start < 0`, `end < start`, or `end > count(src)` | New list of length `end - start` (end exclusive), elementwise copy of that range. The slice may later grow. |
| `list.append(dest, value)` | void | dest | maybe | OOM / `list too large` **trap**; dest stays valid with old `count` | 0.16.1. New last slot. Heap `value` is moved. See `LANGUAGE_SPEC` § 0.16.1. |
| `list.remove(dest, index)` | element | dest | no | **trap** if `index < 0` or `index >= count(dest)` | 0.16.1. Returns the removed element (ownership transfer). Remaining elements shift left, order preserved. |
| `list.pop(dest)` | element | dest | no | **trap** if empty (`Cannot pop from an empty list`) | 0.16.1. Remove last. Ownership transfer. |

There is **no** `list.resize`, `list.push`, `list.cap`, or `list.reserve`. `items[i]` is not `list.get` / `list.set`.

`list.get` / `list.set` are not provided; use `items[i]` and `items[i] = v`. `items[i]` out-of-bounds is still the 0.14.1 index rule, not the `remove` / `pop` trap rule.

## `std/array` — 0.13

0.12 has one sequence type: heap list. A separate array type (fixed length in the type, stack storage, or multi-dimensional) needs the 0.13 type system.

Do not ship `std/array` as an alias of `std/list`. That would freeze a lie.

## `std/sort` — now

Implementation: **runtime heapsort** (`O(n log n)` worst case, in place, **unstable**). `sorted` allocates one copy via `list.copy`. YUKT cannot express a NaN-aware float order with language `<`.

Integer comparison is `<` / `>`. Equal integers may change order.

Float comparison is **not** language `<` when NaNs are present (every IEEE `<` involving NaN is false). Ascending order is:

```
-Inf  <  finite  <  +Inf  <  NaN
```

`-0.0` and `+0.0` compare equal. All NaNs compare equal to each other for ordering; they may still change relative order. Descending is the reverse of that sequence. `sort.search` uses the same ascending order, so `sort.search(items, nan)` after `sort.asc` returns the lowest NaN index, or `-1`.

| Call | Gives | Mutates | Alloc | Failure | Behavior |
| --- | --- | --- | --- | --- | --- |
| `sort.asc(items)` | void | items | no | compile error if not list[integer] or list[float] | Non-decreasing in place. Empty list is a no-op. |
| `sort.desc(items)` | void | items | no | same | Non-increasing in place. |
| `sort.sorted(items)` | list | no | yes | same | Copy, then `sort.asc`. |
| `sort.search(items, value)` | integer | no | no | compile error if `value` does not match element type | Binary search on a list that the caller **must** have sorted with `sort.asc`. Lowest index of `value`, or `-1`. If `items` is not sorted, the result is **undefined**. |

String lists are not sorted in this contract (no `<` on strings in the language). That waits for a language comparison rule or a dedicated `str` order function in a later spec revision.

## `std/math` — now

Implementation: **runtime** to `libm` / integer helpers. Scalar only. No list maps here (`stat` covers reductions; elementwise map can be a `repeat` in user code).

Integer and float overloads are **separate names**. YUKT has no implicit conversion and no overloading by type.

| Call | Gives | Failure | Behavior |
| --- | --- | --- | --- |
| `math.abs_i(x)` | integer | **undefined** if `x` is `INT64_MIN` | Absolute value. |
| `math.abs_f(x)` | float | IEEE | Absolute value. |
| `math.min_i(a, b)` | integer | — | `a` if `a <= b`, else `b`. |
| `math.max_i(a, b)` | integer | — | `a` if `a >= b`, else `b`. |
| `math.min_f(a, b)` | float | IEEE | Numeric min; if either is `NaN`, result is `NaN`. |
| `math.max_f(a, b)` | float | IEEE | Numeric max; if either is `NaN`, result is `NaN`. |
| `math.floor(x)` | float | IEEE | Greatest integer `<= x` as float. |
| `math.ceil(x)` | float | IEEE | Least integer `>= x` as float. |
| `math.sqrt(x)` | float | IEEE | Square root; `x < 0` is `NaN`. |
| `math.pow(x, y)` | float | IEEE | `x` raised to `y`. |
| `math.sin(x)` / `math.cos(x)` / `math.tan(x)` | float | IEEE | Radians. |
| `math.exp(x)` | float | IEEE | `e^x`. |
| `math.log(x)` | float | IEEE | Natural log; `x <= 0` is `NaN` or `-Inf` per IEEE. |

Do not add `math.random` (needs a specified RNG and seed). Do not add bitwise ops (not in the language).

## `std/stat` — now

Implementation: **YUKT** `repeat` over `count(items)`. Sequential: these loops contain a reduction the compiler may parallelize only when the body matches the 0.11 reduction rule (`acc = acc + expr` / `*`) and does not allocate.

| Call | Gives | Failure | Behavior |
| --- | --- | --- | --- |
| `stat.sum_i(items)` | integer | **undefined** on overflow; compile error if not list[integer] | `0` for empty. |
| `stat.sum_f(items)` | float | IEEE rounding; compile error if not list[float] | `0.0` for empty. Parallel association may differ from left-to-right. |
| `stat.min_i(items)` / `stat.max_i(items)` | integer | **trap** if empty | Scan with `<` / `>`. |
| `stat.min_f(items)` / `stat.max_f(items)` | float | **trap** if empty; `NaN` participates as IEEE | Scan. |
| `stat.mean_f(items)` | float | **trap** if empty; compile error if not list[float] | `stat.sum_f(items) / count(items)` as float division. |

There is no `stat.mean_i` (would require int-to-float conversion, which the language forbids). Convert in user code with an explicit float list, or wait for a later conversion spec.

No variance/stddev in this contract (easy to add later; omit rather than underspecify delta degrees of freedom).

## 0.16.2 — `std/io` and `std/env`

**Status: implemented.** Language surface: [`docs/LANGUAGE_SPEC.md`](LANGUAGE_SPEC.md) § 0.16.2. No new reserved words. Programs write `use std/io` and `use std/env`. `say` remains the only stdout print.

| Call | Gives | Alloc | Failure | Behavior |
| --- | --- | --- | --- | --- |
| `io.program()` | string | yes | — | `argv[0]` |
| `io.args()` | list[string] | yes | — | `argv[1..]` |
| `io.read_line()` | result[string] | yes | `eof` / `io` | One line, no terminator. A non-empty unterminated final chunk is `ok(bytes)` with no terminator to strip; `fail("eof")` only when that call reads zero bytes |
| `io.read_all()` | result[string] | yes | `io` | Remaining stdin |
| `env.get(name)` | result[string] | yes | `not_found` / `io` | `getenv` |
| `env.get_or(name, fallback)` | string | maybe | trap if empty name | fallback if unset |
| `env.set(name, value)` | void | no | trap if empty name | `setenv` |
| `env.has(name)` | boolean | no | trap if empty name | `getenv != NULL` |

## 0.16.3 — richer strings

**Status: implemented.** Language surface: [`docs/LANGUAGE_SPEC.md`](LANGUAGE_SPEC.md) § 0.16.3. No new reserved words. No string index syntax. Programs keep `use std/str`.

The eight new rows are in the `std/str` table above. Existing `len` / `at` / `slice` / `find` / `contains` / `starts` / `ends` / `repeat` / `join` / `split` rows are unchanged.

Arguments are borrowed. String results are new Owned allocations and stay sequential in `repeat`. Empty `replace` pattern traps; `to_int` / `to_float` use `fail("bad_int")` / `fail("bad_float")`. Strings stay NUL-terminated bytes. No regex, UTF-8 decoding, or interpolation.

## `std/map` — 0.13

Needs a map type in `docs/LANGUAGE_SPEC.md` (opaque heap object or struct). 0.12 cannot name a map value.

Intended module: `use std/map`. One map value has **one key type and one value type**, inferred from the first `set` (same inference rule as lists). First edition after the type exists: **string keys**, value integer, float, boolean, or string.

Intended calls (not callable in 0.12):

| Call | Gives | Mutates | Alloc | Behavior |
| --- | --- | --- | --- | --- |
| `map.new()` | map | — | yes | Empty map. |
| `map.set(m, key, value)` | void | m | maybe | Insert or replace. May allocate. Not thread-safe. Not for parallel `repeat` bodies. |
| `map.get(m, key)` | value | no | no | **0.13 error/absent** — must not pretend 0.12 sentinels work for every value type. Missing key is an error value or a boolean `has` plus `get` that traps; the language spec for errors decides which. Until then this row is not implementable. |
| `map.has(m, key)` | boolean | no | no | True iff `key` is present. |
| `map.len(m)` | integer | no | no | Number of keys. |
| `map.del(m, key)` | void | m | no | No-op if absent. Does not promise the payload is `free`d before `main` returns unless 0.13 ownership says so. |

No iteration syntax until the language has a way to walk keys. `map.keys(m)` giving `list[string]` is allowed only after 0.13 can allocate `list[string]` of runtime length **and** after missing-key policy is specified.

Capacity growth, if any, is explicit in a later revision. This contract does **not** promise amortized growth, open addressing details, or concurrent maps.

## `std/set` — 0.13

Same blockers as map. First edition: **string** elements.

| Call | Gives | Mutates | Behavior |
| --- | --- | --- | --- |
| `set.new()` | set | — | Empty set. |
| `set.add(s, key)` | void | s | Insert. |
| `set.has(s, key)` | boolean | no | Membership. |
| `set.del(s, key)` | void | s | Remove if present. |
| `set.len(s)` | integer | no | Size. |

No union/intersection until both sets and list-of-string allocation exist.

## `std/file` — 0.13

Needs: C I/O in the runtime, a specified error type, and a rule for path strings (byte paths, host encoding). Not implementable as YUKT.

Intended calls:

| Call | Gives | Behavior |
| --- | --- | --- |
| `file.exists(path)` | boolean | True if the path exists. I/O error → 0.13 error value, not a boolean lie. |
| `file.read(path)` | string | Entire file as bytes. Missing file or read error → error value. |
| `file.write(path, data)` | void | Overwrite or create. Write error → error value. |

No directory listing, sockets, or async I/O in this contract. Paths are strings. Binary vs text is “bytes”; no transcoding.

Do not implement `file.read` in 0.12 with “empty string means failure.”

## `std/json` — 0.13

Needs maps, nested lists of mixed JSON types, or a JSON value type, plus errors. 0.12 lists are homogeneous; a JSON array `[1, "x"]` is not a YUKT list.

Intended calls after a JSON value type exists:

| Call | Gives | Behavior |
| --- | --- | --- |
| `json.parse(s)` | json | Parse UTF-8 JSON text. Invalid input → error value. |
| `json.write(v)` | string | Compact encoding. |

No schema, no comments in JSON, no NaN in numbers. Numbers that do not fit `i64` / `f64` are an error at parse time (exact rule in the 0.13 language/json revision).

## `std/error` — 0.13

This module must not exist until `docs/LANGUAGE_SPEC.md` defines an error type and how `give` / calls propagate it.

Sketch (not normative until the language spec lands):

- An error is a value, not an exception and not `setjmp`.
- I/O and parse APIs give either a success value or an error; mixing them in one untagged union is forbidden unless the language spec defines that union.
- 0.12 traps stay for programmer bugs (bad slice bounds). They do not become error values silently.

## Implementation rules (stage 2)

When a **now** section is implemented:

1. Put YUKT sources under `std/` matching the module path (`std/str.yukt` → `use std/str`).
2. Export only the `share create` names in this file.
3. Runtime C is allowed only for rows marked runtime above, plus any new allocator helper that still obeys `docs/RUNTIME.md` (same bins, not thread-safe, no intra-function free).
4. Do not add compiler special-cases for benchmark names. Sort and sum live in the library.
5. Do not concatenate `std/` into the entry file.
6. Parallel `repeat` inside a library function follows the same independence rules as user code. Allocating helpers stay sequential.
7. Keep 0.11 tests green. Add tests under `tests/stdlib/` (or `tests/modules/` with an injected stdroot that **is** the real `std/` once files exist).

## Validation rules (stage 3)

- **Correctness:** tables in this file, including empty inputs, `-1` search misses, and trap cases.
- **Memory:** allocating functions increase tracked heap; after `yukt_main` returns, release still respects the 8,948,352-byte cache cap. No test may require intra-function `free`.
- **Edges:** `INT64_MIN` for `abs_i`, empty lists for `stat.min_*`, `str.split` with trailing separator, `sort.search` on unsorted input is undefined (do not assert a particular index).
- **Performance:** optional later comparison with C++/Rust in a new `benchmarks/results-*.json`. Never overwrite `benchmarks/results-0.11.json`.

## Out of scope

- Package versions, downloads, or a registry
- Methods, type annotations, generics, `and` / `or` / `not`
- Growable vectors, garbage collection, thread-safe allocation
- Unicode segmentation, regex, datetime, networking, crypto
- Rewriting `count` / `zeros` into `std/`

## First implementation slice

After this spec is accepted, implement **only** gate **now**, in this order:

1. `std/str`
2. `std/list`
3. `std/sort`
4. `std/math`
5. `std/stat`

Stop. Do not start map, set, array, file, json, or error until 0.13 (or a language-spec change) unblocks them.
