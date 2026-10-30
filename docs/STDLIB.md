# YUKT standard library foundations

Status: 0.11 is frozen. The module/`use` system is **specified** in `docs/LANGUAGE_SPEC.md` (0.12) and **not implemented**. Library APIs (maps, JSON, files) are not specified yet.

YUKT 0.11 does not parse `use`. 0.12 must implement the spec first, then specify and add library modules under `std/`.

## What exists now

Compiler builtins (not a library, not user functions):

- `count(list)` — length of a list
- `zeros(n)` — heap list of `n` integers, 16-byte aligned

Runtime (`runtime/yukt_runtime.c`) owns:

- heap lists and concatenated strings
- size-class payload reuse after `yukt_main` returns
- `say`, parallel for, simple reduction combine

`std/` is reserved and empty.

## What 0.12 must specify first

A module/`use` story. Without it, YUKT-written library code cannot be shared across programs. Do not concatenate stdlib source into every compile as a silent hack.

Then, written in YUKT where possible:

- strings and collections
- arrays, lists, maps, and sets
- file handling
- JSON parsing
- sorting and searching
- math and statistics
- error handling

Runtime support only where the language cannot express it (I/O syscalls, maps that need hashing primitives, JSON UTF-8). Optimized sort / matmul / reduction belong in the library, not as compiler special cases for named benchmarks.

## Allocator contract for 0.12

List payloads and string buffers are 16-byte aligned where required. Size-class free lists in the runtime are the backing store 0.12 collections should reuse. Intra-function frees wait for 0.13 ownership. Frozen 0.11 bounds (six classes, four slots, 8,948,352-byte cap) are in `docs/RUNTIME.md`.
