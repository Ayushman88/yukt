# YUKT 0.12 standard library implementation plan

Status: **complete for now-gated modules**. Source of truth: `docs/STDLIB_SPEC.md`. Do not implement 0.13 APIs.

Evidence: `docs/STDLIB_IMPLEMENTATION_REPORT.md`.

## Audit (what the compiler actually does)

- `use std/str` resolves only under `<stdlib-root>/str.yukt`. Development root is CMake `YUKT_STDLIB_DIR` = `${CMAKE_SOURCE_DIR}/std`. Tests may inject another root; stdlib tests use the real `std/`.
- Shared functions are ordinary `share create` symbols, mangled `std$<file>$<name>` (e.g. `std$str$len`).
- `$` is not a portable C identifier. Runtime exports `yukt_str_len` etc. LLVM call sites that target a stdlib IR name call those C symbols. Dummy YUKT bodies are not lowered.
- `count` / `zeros` are polymorphic builtins. User functions have one type scheme for the whole program. `list.*` and `sort.*` are typed **per call**.
- Strings are tracked NUL-terminated `char*` (`YUKT_ALLOC_STR`). Lists are `YuktList` with `kind` 0..4 and a size-class payload. No intra-function free.
- Linking is `clang object.o libyukt_runtime.a -lm`. Math needs `-lm` on Linux (harmless on macOS).

## Path chosen

1. Real modules under `std/*.yukt` with `share create` so `use std/str` and `str.len(...)` stay ordinary module syntax.
2. Runtime C for every operation YUKT cannot express (byte strings, polymorphic list alloc/copy/sort, libm, empty-list traps).
3. YUKT bodies for `stat.sum_i` and `stat.sum_f` only (allocation-free reductions).
4. Semantic analyzer applies spec type schemes for stdlib IR names; skips dummy-body inference for runtime-backed functions.
5. IR lowering omits those dummy functions; LLVM maps `std$…` calls to `yukt_*`.
6. Keywords may be function names after `create` and after `.`, so `str.repeat` parses. Documented in `docs/LANGUAGE_SPEC.md`.

## Phases

1. Runtime primitives — done
2. `std/str.yukt` + typing/lowering — done
3. `std/list.yukt` — done
4. `std/sort.yukt` — done
5. `std/math.yukt` + `-lm` — done
6. `std/stat.yukt` (YUKT sums + runtime min/max/mean) — done
7. `tests/stdlib/` — done (`yukt_stdlib_tests`: 77 passed, 0 failed)
8. Full CTest — done (20/20 passed on macOS arm64, 2026-10-30)

## Non-goals

Maps, sets, arrays, files, JSON, error values, GC, list growth, Unicode, changing allocator caps, rewriting `v0.11.0` / `v0.12.0` / `benchmarks/results-0.11.json`.
