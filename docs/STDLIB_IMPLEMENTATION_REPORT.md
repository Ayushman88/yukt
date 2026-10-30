# YUKT 0.12.1 standard library implementation report

Date: 2026-10-30. Host for this report: macOS arm64 (darwin 25, Apple Silicon). Compiler built with LLVM 23.1.4.

This is evidence that the **now**-gated APIs in `docs/STDLIB_SPEC.md` compile to native executables and pass tests. It is not a claim about Linux until CI on that job is green.

## Implemented APIs

Imported with `use std/<module>` and called as `alias.name(...)`.

### `std/str`

`len`, `at`, `slice`, `find`, `contains`, `starts`, `ends`, `repeat`, `join`, `split`

Byte offsets. Empty string and empty substring behave as specified. `split` keeps trailing empty fields and traps on an empty separator. Invalid slice/`at` bounds and negative `repeat` counts trap.

### `std/list`

`len`, `copy`, `fill`, `reverse`, `slice`

Element type and length preserved. No implicit resize. Integer, float, boolean, string, and nested-list copies were executed.

### `std/sort`

`asc`, `desc`, `sorted`, `search`

Integer and float lists only. `asc`/`desc` mutate in place. `sorted` returns a copy. Search returns the lowest matching index or `-1`. Sort is **unstable heapsort** (`O(n log n)`). Float order is `-Inf < finite < +Inf < NaN`. Unsorted search inputs are not asserted.

### `std/math`

`abs_i`, `abs_f`, `min_i`, `max_i`, `min_f`, `max_f`, `floor`, `ceil`, `sqrt`, `pow`, `sin`, `cos`, `tan`, `exp`, `log`

Separate integer and float names. IEEE NaN/Inf through libm. `min_f`/`max_f` propagate NaN. `abs_i(INT64_MIN)` remains undefined; tests do not assert a numeric result.

### `std/stat`

`sum_i`, `sum_f` — YUKT `repeat` reductions (empty sums are `0` / `0.0`)
`min_i`, `max_i`, `min_f`, `max_f`, `mean_f` — runtime; empty lists trap

## Runtime primitives added

Declared in `runtime/yukt_runtime.h`, implemented in `runtime/yukt_runtime.c`:

- `yukt_str_len`, `yukt_str_at`, `yukt_str_slice`, `yukt_str_find`, `yukt_str_contains`, `yukt_str_starts`, `yukt_str_ends`, `yukt_str_repeat`, `yukt_str_join`, `yukt_str_split`
- `yukt_list_copy`, `yukt_list_fill_i64`, `yukt_list_fill_f64`, `yukt_list_fill_bool`, `yukt_list_fill_cstr`, `yukt_list_fill_list`, `yukt_list_reverse`, `yukt_list_slice`
- `yukt_list_sort_asc`, `yukt_list_sort_desc`, `yukt_list_sorted`, `yukt_list_search`
- `yukt_math_abs_i`, `yukt_math_abs_f`, `yukt_math_min_i`, `yukt_math_max_i`, `yukt_math_min_f`, `yukt_math_max_f`, `yukt_math_floor`, `yukt_math_ceil`, `yukt_math_sqrt`, `yukt_math_pow`, `yukt_math_sin`, `yukt_math_cos`, `yukt_math_tan`, `yukt_math_exp`, `yukt_math_log`
- `yukt_stat_min_i`, `yukt_stat_max_i`, `yukt_stat_min_f`, `yukt_stat_max_f`, `yukt_stat_mean_f`

Allocator caps unchanged: six classes, four slots, 8,948,352-byte retention. Caches remain not thread-safe. No intra-function free.

## Compiler changes

- `compiler/parser`: **`repeat` is the only reserved word allowed as a function name** (`str.repeat`). `create if` / `math.give` are errors (`Reserved word`).
- `compiler/semantic/analyzer.cpp`: per-call type schemes for stdlib IR names; skip dummy-body inference for runtime-backed functions; `NumericList` constraint for `sort`.
- `compiler/ir/lower.cpp`: do not lower dummy bodies of runtime-backed stdlib functions. `stat.sum_*` still lower as ordinary YUKT.
- `compiler/backend/llvm_backend.cpp`: map `std$…` calls to `yukt_*`; link `-lm`.
- `compiler/driver/module.cpp`: `is_stdlib_runtime_ir`.
- Frontend still has no LLVM includes (`compiler/lexer`, `parser`, `ast`, `semantic`).

Dummy `share create` bodies exist so `use` / `share` stay ordinary module syntax. They are not executed.

## Tests executed

Command: `ctest --test-dir build --output-on-failure` on this Mac, 2026-10-30.

| Test | Result |
| --- | --- |
| yukt_lexer_tests | Passed 0.54s |
| yukt_parser_tests | Passed 0.54s |
| yukt_program_tests | Passed 0.57s |
| yukt_semantic_tests | Passed 0.56s |
| yukt_module_tests | Passed 0.52s |
| yukt_ir_tests | Passed 0.63s |
| yukt_native_tests | Passed 15.16s |
| yukt_stdlib_tests | Passed (342 assertions, 0 failed, including heapsort/NaN and `repeat`-only reserved names) |
| hello_tokens / ast / types / check / ir / explain / run | Passed |
| index_run | Passed |
| module_ok_run / module_nested_run | Passed |
| stdlib_str_run | Passed |
| stdlib_all_run | Passed |

**20/20 tests passed.** Total wall time 29.84s.

YUKT programs under `tests/stdlib/` exercised through `yukt_stdlib_tests` (compile to native, execute):

- `str_basic.yukt`, `str_alias.yukt`
- `list_basic.yukt`
- `sort_basic.yukt`, `sort_nan.yukt`
- `math_basic.yukt`
- `stat_basic.yukt`
- `all_modules.yukt`
- trap programs: `str_trap_at`, `str_trap_slice`, `str_trap_repeat`, `str_trap_split`, `list_trap_slice`, `stat_trap_empty`, `stat_trap_mean`
- compile-time type errors: `type_sort_strings`, `type_math_mix`, `type_join_ints`

C runtime checks in the same suite: empty strings, empty substring find, trailing split fields, sort/search duplicates, NaN `min_f`, payload reuse after `yukt_runtime_release`.

Native proof: `yukt build tests/stdlib/all_modules.yukt -o /tmp/yukt-stdlib-all` wrote a **Mach-O 64-bit executable arm64**. Running it printed `3 / [1, 2, 3] / 1 / 6 / 3`. `nm` showed `_yukt_str_len`, `_yukt_list_copy`, `_yukt_math_sqrt`, `_yukt_stat_mean_f`.

## Platform verification

| Check | This session |
| --- | --- |
| macOS arm64 native link + run | Yes (local + CI job `macOS arm64`) |
| Linux x86-64 | Yes on GitHub Actions (`Ubuntu x86-64`, run [41008599932](https://github.com/Ayushman88/yukt/actions/runs/41008599932), commit `3b60075`) |
| `-lm` on generated programs | Yes (`link_executable`) |
| LLVM headers in frontend | None found |
| `benchmarks/results-0.11.json` | Unchanged |
| Allocator cap 8,948,352 | Unchanged |

## Known limitations

- Sort is unstable heapsort. Equal elements may move. NaNs compare equal to each other and sort after `+Inf` on `sort.asc`.
- `sort.search` on an unsorted list is undefined; tests do not assert an index.
- `math.abs_i(INT64_MIN)` is undefined; tests do not execute that argument expecting a value.
- `list.copy` of nested lists copies pointers (elementwise at the list-value level), not a deep clone of inner payloads.
- Allocating stdlib calls are ordinary user calls, so they keep a surrounding `repeat` sequential (allocator is not thread-safe).
- `stat.sum_*` may parallelize when the 0.11 reduction rule matches; association of float sums may differ.

## APIs not implemented and the blocker

| Module | Blocker |
| --- | --- |
| `std/map`, `std/set` | No map/set type in `docs/LANGUAGE_SPEC.md` |
| `std/array` | 0.12 has only heap lists |
| `std/file` | Needs a specified error type and I/O policy |
| `std/json` | Needs maps / mixed-type values / errors |
| `std/error` | Error type must land in the language spec first |

None of these were stubbed.
