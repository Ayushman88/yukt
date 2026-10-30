# YUKT 0.12 implementation plan

Status: **implemented** (module system only). This document is the plan that was followed. Do not concatenate sources. Do not add stdlib APIs.

**Goal:** `yukt build app.yukt` loads `use`d files, type-checks the whole program, and writes one host-native executable. **Done.**

**Architecture:** Lexer → Parser → ModuleGraph → whole-program semantic analysis → one YUKT IR module → one LLVM module → one `.o` + `libyukt_runtime.a`.

**Tech stack:** Handwritten C++20, existing `yukt_compiler` / `yukt_backend`, no new dependencies.

## Global constraints

- Frontend (`lexer`, `parser`, `ast`, `semantic`, `driver`) must not include LLVM headers.
- No package manager, no `..` in module paths, no glob import.
- 0.11 tests stay green. Tag `v0.11.0` is not rewritten.
- `use`, `share`, `as`, and `.` qualification are the only new syntax.

## Files

| Path | Role |
| --- | --- |
| `compiler/lexer/token.hpp`, `lexer.cpp` | `KwUse`, `KwShare`, `KwAs`, `Dot` |
| `compiler/ast/ast.hpp`, `ast.cpp` | `UseDecl`, `FunctionDecl.shared`, qualified `CallExpr` |
| `compiler/parser/parser.cpp` | `{ use } { function }`, `share create`, `IDENT . IDENT (` |
| `compiler/common/source.hpp` | `SourceLocation.file` |
| `compiler/diagnostics/` | Emit the location’s file, not only the engine’s original path |
| `compiler/driver/module.hpp`, `module.cpp` | Resolve, load, graph, cycles, mangle |
| `compiler/driver/driver.cpp` | `analyze_file` loads the graph |
| `compiler/semantic/analyzer.cpp` | Per-module symbols, aliases, `main` only on entry |
| `compiler/ir/lower.cpp` | Merge all modules; mangled call names |
| `compiler/backend/llvm_backend.cpp` | Keep `yukt_` + IR name; `main` → `yukt_main` |
| `compiler/backend/pipeline.cpp` | Still one object file |
| `CMakeLists.txt` | `YUKT_STDLIB_DIR`, `module.cpp`, new tests |
| `tests/modules/` | Multi-file fixtures |

## APIs

```
struct LoadedModule {
    std::string canonical_path;
    std::string mangle_prefix;  // relative path, / → $
    std::string source;
    std::vector<Token> tokens;
    std::unique_ptr<Program> program;
};

struct ModuleGraph {
    std::string entry_dir;
    std::string stdlib_root;
    std::vector<LoadedModule> modules;  // [0] is entry
    std::vector<std::vector<int>> imports;
};

std::string mangle_function(const std::string& prefix, const std::string& name, bool entry_main);
ModuleGraph load_module_graph(const std::string& entry_path, DiagnosticEngine& diagnostics,
                              const std::string& stdlib_root);
```

IR function name is the LLVM suffix: `main` for entry main; otherwise `prefix + "$" + name`.

## Phases

All eight phases landed:

1. Lexer and parser — **done**
2. Resolution and loading — **done**
3. Graph and cycles — **done**
4. Visibility and namespaces — **done**
5. Whole-program types — **done**
6. YUKT IR and LLVM — **done**
7. Native link and run — **done**
8. Diagnostics, regression, docs — **done**
