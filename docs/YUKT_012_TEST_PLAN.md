# YUKT 0.12 test plan

Status: **implemented**. Fixtures live under `tests/modules/`. C++ tests use `analyze_file` / `compile_program`. Existing 0.11 CTest names keep their behavior. Extra CTest names: `yukt_module_tests`, `module_ok_run`, `module_nested_run`.

## Lexer / parser

- `use math` / `use std/math as m` / `share create add`
- `IDENT . IDENT (` parses a qualified call
- `math.add` without `(` is an error
- `use` after `create` is an error
- `.` is a token; `1..5` and `3.14` still work
- 0.11 programs still parse

## Resolution and graph

| Fixture | Expect |
| --- | --- |
| `ok/app.yukt` + `ok/math.yukt` | loads two modules |
| `nested/a.yukt` → `b` → `c` | three modules; `a` cannot call `c` |
| `missing/app.yukt` | `Cannot find module` |
| `std_bare/app.yukt` (`use std`) | error |
| `stdroot/` injected as stdlib | `use std/math` loads the fixture, not repo `std/` |
| `samefile/` two uses of one path | one `LoadedModule` |
| `cycle/a.yukt` ↔ `b.yukt` | `Circular import` |
| `self/app.yukt` | `Circular import` |

## Visibility and names

- Private `helper` is not callable as `math.helper`
- Duplicate alias in one file
- Alias colliding with `create math` or `count` / `zeros`
- Two files both `share create add` — legal
- Duplicate `share create foo` in one file — `Duplicate function`
- Library `create main` — error
- `share create main` — error
- Unqualified `add(...)` does not see imports

## Types and mangling

- `math.add(2, 3)` infers integers across files
- Conflicting call sites on `give x` — type error
- `a/b_c` vs `a_b/c` produce distinct prefixes (`a$b_c` vs `a_b$c`)
- Entry `main` is still `yukt_main` in LLVM

## Native

- `yukt run tests/modules/ok/app.yukt` prints `5`
- Nested import that calls only its direct dependency
- `examples/hello.yukt` and `examples/index.yukt` still run

## Regression

All 0.11 CTest targets pass: lexer, parser, program, semantic, IR, native, hello_*, index_run.
