# YUKT 0.12 design — Modules, then a standard library

Status: **module system implemented**. 0.11 remains frozen (`v0.11.0`). Standard-library APIs are not part of this milestone; `std/` stays empty.

## Order

1. Specify modules and imports (this document + the spec section). **Done.**
2. Implement the module system with tests. **Done.**
3. Specify stdlib APIs (strings, collections, files, JSON, math) as a separate spec pass.
4. Implement those APIs in YUKT under `std/`, with runtime only where the language cannot express them.

Do not skip to (3) or (4) by concatenating library source into every compile.

## Choices locked in the spec

| Topic | Rule |
| --- | --- |
| Syntax | `use path`, optional `as alias`; `share create` exports |
| Visibility | Default private; only `share create` is callable as `alias.name(...)` |
| Namespaces | Last path component is the alias; no glob import |
| Resolution | `std/...` from compiler `std/` only; else importer dir, then entry dir |
| Compilation | Whole-program native binary; mangled `yukt_<prefix>$<name>` (`/` → `$`) |
| Cycles | Error, with the cycle listed |
| `main` | Only the entry file; never `share` |

`.` is module qualification on a callee, not methods. Methods wait for 0.13.

## Why not the alternatives

- **Public-by-default (Python):** a stdlib file would export helpers the language did not mean to commit to. Default private matches a small, explicit language.
- **Allow import cycles:** 0.12 compilation is a DAG. Mutual recursion stays in one file until a later spec says otherwise.
- **Package manager:** out of scope. Files on disk only.

## Frozen 0.11 that 0.12 must not disturb

- Host-native compilation, no cross-compilation
- Numerical semantics (signed overflow undefined; float reduction rounding may change with chunking and CPU count)
- Allocator caps in `docs/RUNTIME.md`
- Benchmarks in `docs/BENCHMARKS.md` and `benchmarks/results-0.11.json`
