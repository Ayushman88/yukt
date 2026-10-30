# YUKT 0.12 design audit

Status: **implemented**. Audit of `docs/LANGUAGE_SPEC.md` § 0.12 against the 0.11 compiler. Spec changes locked here are applied in the language spec. The compiler implements this audit; it does not invent extra syntax.

0.11 remains tagged `v0.11.0`. This milestone is the **module system only**. No stdlib APIs, no package manager, no source concatenation.

## What the 0.11 compiler assumes

One file → one `Program` → one `SemanticInfo` keyed by unqualified name → one `IrModule` → one LLVM module → one `program.o` + `libyukt_runtime.a`.

That one-object shape stays. Frontend, diagnostics, semantic keys, and mangling must become multi-file.

## Ten edge cases

### 1. Two module paths, identical mangled names

If `/` is replaced by `_`, `a/b_c` (`a$` wait: `a/b_c.yukt`) and `a_b/c` both mangle to `a_b_c`.

**Fix:** encode `/` as `$` (not a legal identifier character). `a/b_c` → `a$b_c`, `a_b/c` → `a_b$c`. After mangling, duplicate LLVM names are still a compile error.

### 2. Indirect mutual import

`A → B → C → A` is a cycle. Self-`use` is a cycle of length 1. Both are `Circular import` with a note listing one cycle as file paths.

### 3. Duplicate aliases

Two `use` lines in one file that bind the same alias are an error, even if they resolve to the same file.

### 4. Private function access

`math.helper(...)` when `helper` is not `share` is an error **at the `.`**, with a note of the shared names of that module.

### 5. Incompatible inferred parameter types

One `TypeContext` for the whole program. A shared function’s types unify from its body **and** all call sites. `m.id(1)` and `m.id(true)` on `give x` is a type error.

### 6. Identical function names in different modules

Legal. `share create add` may exist in two files. Importers distinguish them by alias. Duplicate names **in one file** remain an error.

### 7. Two `use` strings, same canonical file

`std::filesystem::canonical` after the file exists. One `LoadedModule`. Each importer keeps its own alias.

### 8. Relative paths and `..`

Module path components are identifiers. `.`, `..`, `.yukt` suffixes, and empty components are errors. Resolution: importer directory, then entry directory. Importer-relative wins when both exist as different files.

### 9. Missing or unreadable files

Diagnostic on the `use` line. Notes list the paths that were tried. Permission / open failures use the same title `Cannot find module`.

### 10. Standard library after install

`<stdlib-root>` is `YUKT_STD` if set, else compile-time `YUKT_STDLIB_DIR`, else `<executable>/../share/yukt/std`. `use std` is an error (need `use std/name`). The project tree is never searched for `std`. Tests inject a fixture root. **No shipping math/JSON/file APIs.**

## Other locked rules

- **`use` is not transitive.** A uses B uses C does not let A call C.
- **`--tokens`** lexes only the named file. `--ast` / `--types` / `--emit-*` / `--explain` / `build` / `run` load the graph. `--ast` prints each loaded file.
- **Entry stem.** If the entry file defines any function other than `main`, its stem must be a YUKT identifier.
- **Diagnostics.** Every location carries a file path.
- **Alias vs parameter.** An alias is only valid as `alias.name(...)`. Unqualified `alias` is a local or parameter if one exists; it is not a namespace value.
- Entry `create main` remains LLVM `yukt_main`. Other functions: `yukt_<prefix>$<name>` where `<prefix>` is the path relative to the entry directory with `/` → `$`, or `std$...` for the standard library.

## Architecture (no extra class per noun)

| Name | Location |
| --- | --- |
| ModuleResolver | `resolve_use()` in `compiler/driver/module.cpp` |
| ModuleLoader | `load_module_graph()` in the same file |
| ModuleGraph | `struct ModuleGraph` in `compiler/driver/module.hpp` |
| CycleDetector | DFS inside `load_module_graph` |
| ModuleNameMangler | `mangle_function()` |
| Symbol table, exports, whole-program analyzer | `compiler/semantic/analyzer.cpp` |

Frontend stays LLVM-free.
