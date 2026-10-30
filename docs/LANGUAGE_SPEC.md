# YUKT Language Specification

Version: 0.11 frozen; 0.12 module system specified, not implemented.  
Status: 0.11 syntax, type checking, native host executables, optimizations, automatic parallelization (including simple +/* reductions), SIMD, execution explanation, and host-native CI. Numerical semantics for signed overflow and float rounding are defined in this document. The 0.12 module and import system below is language design only; the 0.11 compiler must still reject it.

This document is the source of truth for YUKT syntax. If the compiler and this file disagree, that is a bug.

## Design

YUKT is an English-inspired, statically typed, compiled language. The first syntax is small on purpose:

- indentation defines blocks
- assignments introduce names
- `create` declares functions
- `say` writes output
- `give` returns a value
- `repeat` iterates a range
- `if` / `otherwise` choose a branch

The compiler does not invent additional syntax beyond this document.

## Source files

- Extension: `.yukt`
- Encoding: UTF-8. Identifiers are ASCII `[A-Za-z_][A-Za-z0-9_]*`.
- Newlines: `\n` or `\r\n`
- Comments: `#` to the end of the line
- Indentation: spaces only. Tabs are an error.
- Columns are counted in bytes, 1-based.

A program is a sequence of function declarations. There is no executable top-level code outside `create`.

## Reserved words

```
create  say  if  otherwise  repeat  in  give  true  false
```

These cannot be used as names.

## Lexical tokens

| Token | Form |
| --- | --- |
| Identifier | `name`, `age`, `items` |
| Integer | `21`, `0`, `10` |
| Float | `3.14`, `0.5` |
| String | `"Hello"` with escapes `\\`, `\"`, `\n`, `\t` |
| Boolean | `true`, `false` |
| Operators | `+ - * / % == != < <= > >= =` |
| Punctuation | `( ) [ ] , :` |
| Range | `..` |
| Layout | `NEWLINE`, `INDENT`, `DEDENT` |

`1..5` is tokenized as integer `1`, `..`, integer `5`. A single `.` is not a token.

Unterminated strings, unknown characters, tabs, and indentation that does not match an open block are lexer errors.

## Indentation

YUKT uses Python-style layout tokens:

1. A line that contains only spaces and/or a comment does not change indentation.
2. If a non-empty line is indented more than the current block, the lexer emits `INDENT`.
3. If it is indented less, the lexer emits one `DEDENT` per closed block.
4. A dedent must land on a previous indentation level. Otherwise the compiler reports `Inconsistent indentation`.

Blocks are introduced by `:` at the end of a clause, a newline, and an indented suite. An empty suite is an error.

## Grammar

```
program        ::= { function }

function       ::= "create" IDENT [ "(" [ IDENT { "," IDENT } ] ")" ] ":" suite

suite          ::= NEWLINE INDENT { statement } DEDENT

statement      ::= assignment
                 | index_assign
                 | call_stmt
                 | say_stmt
                 | give_stmt
                 | if_stmt
                 | repeat_stmt

assignment     ::= IDENT "=" expression NEWLINE
index_assign   ::= postfix "=" expression NEWLINE
call_stmt      ::= call NEWLINE
say_stmt       ::= "say" expression NEWLINE
give_stmt      ::= "give" expression NEWLINE

if_stmt        ::= "if" expression ":" suite
                   [ "otherwise" ":" suite ]

repeat_stmt    ::= "repeat" IDENT "in" expression ".." expression ":" suite

expression     ::= equality
equality       ::= comparison { ( "==" | "!=" ) comparison }
comparison     ::= term { ( "<" | ">" | "<=" | ">=" ) term }
term           ::= factor { ( "+" | "-" ) factor }
factor         ::= unary { ( "*" | "/" | "%" ) unary }
unary          ::= [ "-" ] unary | postfix
postfix        ::= primary { "[" expression "]" }
primary        ::= INTEGER | FLOAT | STRING | "true" | "false"
                 | IDENT | call | list | "(" expression ")"
call           ::= IDENT "(" [ expression { "," expression } ] ")"
list           ::= "[" [ expression { "," expression } ] "]"
```

Operator precedence, highest first:

1. index `[]`
2. unary `-`
3. `* / %`
4. `+ -`
5. `< > <= >=`
6. `== !=`

All binary operators are left-associative.

## Features in 0.01

### Variables

```yukt
name = "Ayush"
age = 21
ready = true
items = [1, 2, 3]
```

Assignment is the only name-binding form. There is no `let`, `var`, or type annotation syntax yet.

### Output

```yukt
say "Hello"
say "Hello, " + name
```

`say` is a statement. It is not a function.

### Functions

```yukt
create greet(name):
    say "Hello, " + name

create main:
    greet("World")
```

`create main:` and `create main():` are both valid. Parameters are names only.

### Conditions

```yukt
if age > 18:
    say "adult"
otherwise:
    say "young"
```

`otherwise` is optional and must immediately follow the `if` suite at the same indentation.

### Loops

```yukt
repeat i in 1..10:
    say i
```

The range bounds are integers. The range is inclusive on both ends: `repeat i in 1..5` binds `i` to 1, 2, 3, 4, and 5. The loop variable exists only inside the loop body.

### Indexing

```yukt
items = [10, 20, 30]
say items[0]
items[1] = 21
```

`items[i]` reads one element. `items[i] = value` replaces one element. The collection must be a list. The index must be an integer. The first element is at index `0`.

Lists do not grow. `items = []` still cannot be filled later. Use `zeros(n)` to allocate a list of `n` integer zeros, then assign slots.

Integer and float index reads and writes are unchecked so numeric loops can vectorize. An out-of-range index on those lists is undefined. Boolean, string, and nested-list indexes still trap in the runtime.

### Built-in functions

These names are provided by the compiler when the program does not define them:

| Call | Meaning | Type |
| --- | --- | --- |
| `count(items)` | number of elements | integer |
| `zeros(n)` | new list of `n` integer zeros | list[integer] |

A user `create count` or `create zeros` shadows the built-in.

### Return

```yukt
create answer:
    give 42
```

### Comments

```yukt
# comment
```

## Types

YUKT is statically typed. Milestone 0.02 infers types from literals, operators, assignments, and calls. There are no type annotations.

| Literal / form | Type |
| --- | --- |
| `21` | integer |
| `3.14` | float |
| `"Hello"` | string |
| `true` / `false` | boolean |
| `[1, 2, 3]` | list[integer] |
| function with no `give` | void |

There is no implicit conversion. `1 + 1.5` is an error. `"age " + 21` is an error.

### Operators

| Operator | Rule | Result |
| --- | --- | --- |
| `+` on strings | both operands string | string |
| `+ - * /` on numbers | both operands the same numeric type | that type |
| `%` | both operands integer | integer |
| unary `-` | integer or float | that type |
| `< > <= >=` | both operands the same numeric type | boolean |
| `== !=` | both operands the same type | boolean |

### Inference

- A variable's type is the type of its first assignment. Later assignments must match.
- Assignments bind in the enclosing function, not in `if` / `otherwise` blocks.
- A name may be used only after an assignment appears earlier in that function.
- Parameter types are inferred from uses in the body and from argument types at call sites.
- If a parameter or empty list never gets a concrete type, the compiler reports `Cannot infer type`.
- All `give` expressions in a function must have the same type. That type is the function's return type.
- A function with no `give` returns void and cannot be used as an expression.

### Scopes

- Function names are global. Duplicate function names are an error.
- Variables cannot reuse a function name.
- `repeat` iterators are scoped to the loop body.
- A loop variable cannot be reassigned and cannot collide with an existing name.

### Statements

- `if` conditions must be boolean. Integers are not treated as true or false.
- `repeat` bounds must be integers.
- `say` accepts any non-void value.
- Call argument count and types must match the callee.
- A list index must be an integer. Only lists can be indexed.
- `count` takes one list. `zeros` takes one integer.

## Memory

- Integers, floats, and booleans are stack values. LLVM `mem2reg` promotes them when it can.
- Lists and strings live on the heap. List payloads are 16-byte aligned. The runtime releases them when `main` returns. Size-class reuse is bounded and not thread-safe; allocating loops stay sequential. Exact bins, slot limits, and retained bytes are in `docs/RUNTIME.md`.
- Function calls use the native C convention after name mangling: `create add` becomes `yukt_add`.

## Automatic parallelization

The compiler may run a `repeat` loop on several cores when every iteration is independent:

- the body has no `say`, `give`, or user function call
- list writes use an index that mentions the loop iterator
- estimated work (trip count × body cost) is at least 1_000_000 operations, or the trip count is not a compile-time constant
- a loop nested inside another `repeat` only parallelizes when its trip count is a compile-time constant of at least 1_000_000
- unknown nested inner trip counts are estimated as 256 iterations for cost
- `count(xs)` folds when `xs` is a list literal or `zeros(n)` with a compile-time integer `n`

Trip counts below 64 stay sequential. The runtime also stays sequential when only one CPU is available or when `trip × work-per-iteration` is below a dispatch tax (`100000 × ncpu`). Larger loops split into one inclusive range per CPU and reuse a thread pool (libdispatch on macOS, a persistent pthread pool on Linux). Overlapping writes to the same list slot from two iterations are undefined if the loop was parallelized.

A simple reduction of the form `acc = acc + expr` or `acc = acc * expr` (or the commuting `expr + acc` / `expr * acc`), where `acc` is integer or float, `expr` does not mention `acc`, and there is only one such accumulator, may run in parallel. Each chunk computes a local partial from the operator identity (0 for `+`, 1 for `*`) and combines with `+` or `*` after the range. Other reductions, including `-`, mixed scalars, and reductions inside `if`, stay sequential. A loop that allocates (`zeros`, list literals, or string concatenation) stays sequential because the runtime allocator is not thread-safe.

## Numerical semantics

Integers are signed 64-bit (`i64`). Floats are IEEE-754 binary64 (`f64`).

**Signed integer overflow is undefined behavior.** This applies to `+`, `-`, `*`, unary `-`, sequential code, parallel code, and reduction combines. LLVM lowering uses `nsw` add/mul. The runtime combine (`yukt_reduce_i64`) uses C `int64_t` `+=` / `*=`, which is also undefined on overflow. YUKT does **not** wrap around (two’s complement wrap), saturate, or trap. A program that overflows a signed integer is invalid; the result is not a language-level wrapping integer.

**Integer `+` and `*` reductions that do not overflow** are associative. Parallel chunk partials combined with `+` or `*` match a sequential left fold.

**Floating-point `+` and `*` are not associative.** A parallel reduction splits the trip into one chunk per CPU, folds each chunk, then combines the partials. That association — and therefore the **rounding result** — may differ from a left-to-right sequential fold, and may differ when **chunking or CPU count** changes. LLVM vectorization of a sequential float loop may also change association and rounding. YUKT does not promise bitwise-identical float results across sequential vs parallel execution, CPU counts, or LLVM versions. Different rounding is specified behavior, not a bug.

**Division** of integers truncates toward zero. Division by zero is undefined.

`yukt --explain file.yukt` prints the host triple, each loop’s sequential / parallel plan, and only reports parallelization or vectorization as successful when those facts appear in generated LLVM IR.

## SIMD

After lowering, the LLVM O3 pipeline runs, including loop vectorization and SLP. Integer and float list access is a GEP load/store so LLVM can vectorize numeric loops. The compiler targets the host CPU, not a generic baseline.

## Entry point

Every program must contain exactly one `create main` with no parameters:

```yukt
create main:
    say "YUKT is running!"
```

`create main:` and `create main():` are both valid. `create main(name):` is an error.

If `main` gives an integer, that value is the process exit code. Other return types still exit 0.

## Execution

`say` writes a value followed by a newline.

`yukt build file.yukt -o name` emits a native executable for the host. `yukt run file.yukt` compiles to a temporary binary and executes it. Neither command is an interpreter.

## Out of scope for 0.11

The 0.11 compiler must not parse the following as if they were implemented. Modules and imports are specified in the next section for 0.12; they are still rejected until that milestone’s compiler work starts.

- modules and imports (`use`, `share`, `as`, `.` qualification) — specified for 0.12, not implemented
- classes or structs
- `and` / `or` / `not`
- bitwise operators
- `for`, `while`, `break`, `continue`
- type annotations
- methods or other member access (`.` is only module qualification in 0.12, not `list.len`)
- an interpreter or bytecode VM
- package versions, remotes, or a package manager

## 0.12 — Modules and imports

**Status: specified, not implemented.** Do not change the compiler until an implementation plan is approved. Syntax in this section is the source of truth for 0.12.

0.12’s job is a module/`use` story so a YUKT-written standard library can exist as real files. It does not add maps, JSON, files, or new collection types. Those APIs wait for a later 0.12 library spec after this system exists.

### Purpose

A program is still one native executable. The entry file has `create main`. Other `.yukt` files are modules. A module is a file of functions. There is no executable top-level code.

### New reserved words

```
use  share  as
```

These cannot be used as names in 0.12. They are not reserved in the 0.11 compiler.

### Grammar additions

`use` lines come first, then functions. A `use` after a `create` is an error.

```
program        ::= { use_stmt } { function }

use_stmt       ::= "use" module_path [ "as" IDENT ] NEWLINE

module_path    ::= IDENT { "/" IDENT }

function       ::= [ "share" ] "create" IDENT [ "(" [ IDENT { "," IDENT } ] ")" ] ":" suite

call           ::= callee "(" [ expression { "," expression } ] ")"
callee         ::= IDENT | IDENT "." IDENT
```

`/` in a `use` path is a path separator, not division. `.` between two identifiers in a callee is **module qualification**, not a float and not a method. A single `.` that is not part of a float literal becomes a token in 0.12. `math.sqrt` without `(` is an error: YUKT has no first-class function values.

### Visibility

- `create name` is **private** to its source file. Other files cannot call it.
- `share create name` is **exported**. Importers call it as `alias.name(...)`.
- `share` is only valid immediately before `create`. `share` on any other form is an error.
- `create main` cannot be `share`. `share create main` is an error.
- Only the **entry file** (the path passed to `yukt build` / `yukt run`) may define `create main`. A library module that contains `main` is an error.
- The whole program still has exactly one `create main`, with no parameters.
- There are no module-level variables. Only functions are bindable across files.
- Compiler builtins `count` and `zeros` remain visible in every file without `use`, as in 0.11.

```yukt
# math.yukt
share create add(a, b):
    give a + b

create helper(a):
    give a + 1
```

```yukt
# app.yukt
use math

create main:
    say math.add(2, 3)
    # math.helper(1) is an error: helper is private
    # add(2, 3) is an error: no glob import; the name is math.add
```

### Namespaces

- `use a/b` binds the **last path component** as the alias (`b`), unless `as` names a different alias.
- `use std/math` binds `math`. `use std/math as m` binds `m`.
- The alias is a namespace, not a value. It is only valid as the left side of `alias.name(...)`.
- Two `use` lines in the same file that bind the same alias are an error, even if they resolve to the same file.
- An alias must not collide with a `create` name in that file or with `count` / `zeros`.
- Unqualified `name(...)` looks up functions in the **current file** and builtins. It does not search imported modules.
- `alias.name` looks up `name` only among **shared** functions of that module. Unknown or private names are errors at the `.` (file, line, column, and a note of what is shared).

### Dependency resolution

Module identity is the canonical file path of a `.yukt` file. Two `use` strings that resolve to the same file are one module.

A module path is one or more identifiers separated by `/`. It must not contain `.`, `..`, a `.yukt` suffix, or an empty component.

Resolve `use p`:

1. If the first component is `std`, the path must have at least two components. `use std` is an error. `use std/math` resolves only to `<stdlib-root>/math.yukt`, where `<stdlib-root>` is the compiler’s `std/` directory (source tree or install). The project tree is **not** searched for `std`. User modules cannot start with `std`.
2. Otherwise, try `<directory of the importing file>/p.yukt`.
3. Otherwise, try `<directory of the entry file>/p.yukt`.
4. Otherwise report `Cannot find module p`, with notes listing the paths that were tried.

Importer-relative (step 2) wins over entry-relative (step 3) when both exist as different files.

No search of `PATH`, the network, or a package registry. 0.12 has no versions and no package manager.

If the file exists but is not readable UTF-8 YUKT source, that is a diagnostic on the `use` line.

### Compilation

- `yukt build entry.yukt` and `yukt run entry.yukt` load the entry file, then recursively load each used module. Inspection flags (`--ast`, `--types`, `--emit-ir`, `--emit-llvm`, `--explain`) do the same.
- Compilation is **whole-program**: one host-native executable. 0.12 does not emit separately linked YUKT libraries.
- The frontend stays LLVM-free. The driver owns the module graph; semantic analysis is whole-program; each function still lowers through YUKT IR.
- Do not concatenate library sources into the entry file as a parse hack.
- LLVM / C names: entry `create main` remains `yukt_main`. Every other function is `yukt_<module>_<name>`. `<module>` is the file path relative to the entry directory, without `.yukt`, with `/` replaced by `_`. Standard-library files use `std/` plus the path under `<stdlib-root>` (so `std/math.yukt` → `std_math`). A module file that is not under the entry directory and not under `<stdlib-root>` is an error. `share create add` in `std/math` is `yukt_std_math_add`. Private functions are mangled the same way so they cannot collide across files. The entry file stem is used for non-`main` functions in the entry module.
- Two modules may both `share create add`; importers distinguish them by alias.
- Duplicate function names in one file remain an error.

### Circular imports

**Import cycles are errors.** Self-`use` is a cycle.

The compiler builds a directed graph with an edge from importer to importee for each `use`. If the graph is not a DAG, it reports `Circular import` on each `use` that participates, with a note listing one cycle as file paths:

```text
Circular import
note: a.yukt -> b.yukt -> a.yukt
```

0.12 has no module-level initialization, but cycles are still forbidden so compilation stays a DAG and mutual recursion cannot span files. Mutual recursion belongs in one file. Relaxing this would be a later spec change, not a silent compiler choice.

### Standard library files

`std/` stays the reserved tree for YUKT-written library modules. 0.12 may add files there only after their APIs are specified. Until then the directory may be empty. Builtins `count` and `zeros` do not move into `std/` in this design.

### Diagnostics

Every module error has file, line, and column of the `use`, `share`, or qualified call. Missing modules, private access, alias collisions, reserved `std` misuse, and cycles must not be ignored.

### Non-goals for this design

- Selective import (`use add from math`) and glob import (`use math.*`)
- Re-export (`share use math`)
- First-class function values
- Method call / field access
- Separate compilation to `.o` per module as a user feature
- Package versions, lockfiles, or downloads

