# YUKT standard library foundations

Status: 0.12.1 ships the **now**-gated modules. The API contract remains **`docs/STDLIB_SPEC.md`**. Maps, sets, arrays, files, JSON, and error values wait for 0.13 language types.

YUKT 0.12 parses `use`, `share`, and qualified calls. Programs import the library with `use std/str` (and the other shipped modules) and call `alias.name(...)`.

## What exists now

Shipped modules (`std/*.yukt`, `use std/...`):

- `std/str` — byte-string helpers
- `std/list` — fixed-length list helpers
- `std/sort` — in-place sort, sorted copy, binary search
- `std/math` — scalar integer/float math
- `std/stat` — numeric list reductions

Compiler builtins (not a library, not user functions):

- `count(list)` — length of a list
- `zeros(n)` — heap list of `n` integers, 16-byte aligned

Runtime (`runtime/yukt_runtime.c`) owns heap lists, concatenated strings, size-class payload reuse, `say`, parallel for, reduction combine, and the C primitives behind the shipped modules.

## Specification

The catalog, ship-gates, allocator limits, and deferred 0.13 APIs are in [`docs/STDLIB_SPEC.md`](STDLIB_SPEC.md). Implementation notes and test evidence are in [`docs/STDLIB_IMPLEMENTATION_REPORT.md`](STDLIB_IMPLEMENTATION_REPORT.md).

Do not concatenate stdlib source into every compile. Sort and sum live in the library, not as compiler special cases for named benchmarks.

## Allocator contract for 0.12

List payloads and string buffers are 16-byte aligned where required. Size-class free lists in the runtime are the backing store 0.12 collections reuse. Intra-function frees wait for 0.13 ownership. Frozen 0.11 bounds (six classes, four slots, 8,948,352-byte cap) are in `docs/RUNTIME.md`.
