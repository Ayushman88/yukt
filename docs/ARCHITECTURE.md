# YUKT Architecture

Version: 0.11

## Pipeline

```
.yukt source
    → Lexer          tokens, including INDENT / DEDENT
    → Parser         abstract syntax tree
    → Semantic       types, scopes, validation
    → YUKT IR        backend-neutral intermediate form
    → LLVM backend   LLVM IR, object emission
    → clang + runtime
    → native executable
```

`yukt file.yukt` type-checks. `yukt build` and `yukt run` continue through LLVM and produce a real host binary. There is no interpreter path.

## Why this split

The frontend is handwritten so the language can stay small and explicit. LLVM is the native backend. Keeping LLVM out of the parser, AST, semantic analyzer, and YUKT IR means the language frontend can change backends later.

YUKT IR sits between semantic analysis and LLVM. That IR is the stability boundary: LLVM-specific types stay in `compiler/backend`.

## Directory responsibilities

```
compiler/
  common/         SourceLocation, SourceSpan, SourceFile
  diagnostics/    DiagnosticEngine
  lexer/          Token, Lexer
  ast/            expression, statement, and program nodes
  parser/         recursive-descent Parser
  driver/         file loading and frontend orchestration
  semantic/       types, scopes, inference, validation
  ir/             YUKT IR and AST lowering
  backend/        LLVM lowering, object emission, linking
  main.cpp        CLI
```

`yukt_compiler` is the LLVM-free frontend and IR library. `yukt_backend` is the only target allowed to include LLVM headers. Generated programs link `libyukt_runtime.a`.

## Ownership

AST nodes are heap-allocated with `std::unique_ptr`. The parser produces a `Program` that owns every function, statement, and expression. Tokens own their lexeme strings so they do not depend on the source buffer after lexing completes.

There are no raw owning pointers.

## Lexer

The lexer is indentation-aware:

- Spaces at the start of a non-empty line become `INDENT` or `DEDENT` tokens.
- Comment-only and blank lines do not affect the indent stack.
- Tabs are rejected.
- `1..5` is recognized as a range, not a float.
- Every token stores a 1-based line and column.

Invalid characters and unterminated strings produce diagnostics and do not crash the lexer.

## Parser

The parser is recursive descent and follows the grammar in `docs/LANGUAGE_SPEC.md`.

- Precedence is encoded as layered parse functions (`equality` → `comparison` → `term` → `factor` → `unary` → `primary`).
- Blocks require `:`, a newline, and `INDENT`.
- After an error, the parser synchronizes to a newline or a statement keyword and continues when it can.
- A successful parse of invalid source is a bug.

## Diagnostics

Diagnostics start with a GNU first line, then the human block:

```
examples/hello.yukt:5:12: error: Unexpected token
ERROR: Unexpected token
File: examples/hello.yukt
Line: 5, Column: 12

<what went wrong>

    <source line>
           ^

Suggestion: <how to fix it>
note: examples/hello.yukt:3:5: <related location>
```

Color is used only when stderr is a TTY and `NO_COLOR` is unset. The compiler exits with status 1 when any error was reported.

## Semantic analysis

`compiler/semantic` walks a parsed program and does not depend on LLVM.

1. Collect every `create` declaration. Function names are unique.
2. Give each parameter and return type a fresh type variable.
3. Analyze each body: resolve names, infer assignments, check operators, unify calls.
4. Apply deferred numeric/string constraints after all call sites have been seen.
5. Require exactly one parameterless `create main`.
6. Report unresolved parameters, empty lists, and return types that never become concrete.

`--tokens` and `--ast` stop before this pass so an ill-typed program can still be inspected. `yukt file.yukt` and `yukt --types file.yukt` run it. Code generation runs only after type checking succeeds.

## YUKT IR

`compiler/ir` lowers a typed program to a small block-based IR:

- named locals
- typed temporaries
- arithmetic, comparison, and string concatenation
- calls, lists, index get/set, `count`, `zeros`, and `say`
- `if` / `otherwise` and inclusive `repeat` control flow
- `parallel_for` when independence analysis accepts a loop

`--emit-ir` prints this form. It does not contain LLVM types.

## LLVM backend

`compiler/backend` is the only module that includes LLVM headers.

1. Lower YUKT IR to LLVM IR.
2. Run the LLVM O3 new-pass-manager pipeline (mem2reg, loop vectorize, SLP).
3. Emit a host object file with a `TargetMachine` for the host CPU.
4. Link with `clang` and `libyukt_runtime.a`.

Integer and float list elements are accessed with GEPs through `YuktList.data` so LLVM can vectorize. Counted `repeat` loops lower to `k < niter` so LLVM sees a canonical trip count. Independent loops with estimated work ≥ 1e6 become an outlined range callback plus `yukt_parallel_for(..., work_per_iter)`. Simple `+`/`*` reductions may outline the same way and combine partials. The runtime may still sequentialize small live trip counts. List payloads are 16-byte aligned. Heap lists and concatenated strings are released when `yukt_main` returns; size-class payloads may be reused by the next allocation (see `docs/RUNTIME.md`). Loop decisions are recorded on YUKT IR and printed by `yukt --explain`. The object file targets the **host** triple and CPU; linking uses the LLVM clang the compiler was built against when it exists.

The runtime implements `say`, string concatenation, lists, heap release, and the parallel loop helper. YUKT functions are emitted as `yukt_<name>`. A C `main` wrapper calls `yukt_main`. Signed overflow is undefined; float reduction rounding may change with chunking and CPU count (`docs/LANGUAGE_SPEC.md`).

## CLI

| Command | Behavior |
| --- | --- |
| `yukt --tokens file.yukt` | Lex and print tokens |
| `yukt --ast file.yukt` | Parse and print the AST |
| `yukt --types file.yukt` | Type-check and print inferred signatures |
| `yukt --emit-ir file.yukt` | Print YUKT IR |
| `yukt --emit-llvm file.yukt` | Print LLVM IR |
| `yukt --explain file.yukt` | Print verified loop execution decisions |
| `yukt build file.yukt [-o exe]` | Emit a native executable |
| `yukt run file.yukt` | Compile, link, and execute |
| `yukt file.yukt` | Parse and type-check |
| `yukt --help` | Usage |
| `yukt --version` | `0.11` |

`build` and `run` fail if type checking fails. They do not interpret the program.

## Testing

CTest runs frontend, IR, and native suites:

- `yukt_lexer_tests`
- `yukt_parser_tests`
- `yukt_program_tests`
- `yukt_semantic_tests`
- `yukt_ir_tests`
- `yukt_native_tests`
- `yukt --tokens`, `--ast`, `--types`, `--emit-ir`, `--explain`, type-check, and `run` of `examples/hello.yukt`

Native tests execute compiled binaries and compare stdout.
