# YUKT Language Specification

Version: 0.13.2 (ownership specified on this tree; `v0.13.1` exists locally and is not pushed; 0.13.3 not started). 0.12 remains tagged `v0.12.1`. 0.11 remains tagged `v0.11.0`.  
Status: 0.12 language plus **records** ([0.13.1 — Records](#0131--records)) and **ownership** ([0.13.2 — Ownership](#0132--ownership)). `ref`, `foreign`, error values, and 0.13 stdlib expansion are **not** part of this slice. Numerical semantics for signed overflow and float rounding are unchanged.

This document is the source of truth for YUKT syntax. **0.13.1 is implemented; 0.13.2 is specified below and being implemented.** If the compiler and this file disagree, that is a bug.

## Design

YUKT is an English-inspired, statically typed, compiled language. The first syntax is small on purpose:

- indentation defines blocks
- assignments introduce names
- `create` declares functions
- `record` / `share record` declares a named product type (0.13.1)
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

A program is `use` lines, then a sequence of `record` declarations and `create` functions, in any order. There is no executable top-level code outside `create`. Records are types, not executable declarations.

## Reserved words

```
create  say  if  otherwise  repeat  in  give  true  false
use  share  as
record
```

These cannot be used as variable names, parameter names, aliases, `repeat` iterators, record names, field names, or function names, with one exception: **`repeat`** may be a function name after `create` / `share create` and after `.` in a qualified call, so `std/str` can export `str.repeat(...)`. `create if`, `record if`, and `math.give(...)` remain errors. Statement position is unchanged: `repeat i in 1..5:` is still a loop. `use`, `share`, and `as` are reserved from 0.12. `record` is reserved from 0.13.1. `ref`, `foreign`, `ok`, and `fail` are **not** reserved in this slice.

## Lexical tokens

| Token | Form |
| --- | --- |
| Identifier | `name`, `age`, `items` |
| Integer | `21`, `0`, `10` |
| Float | `3.14`, `0.5` |
| String | `"Hello"` with escapes `\\`, `\"`, `\n`, `\t` |
| Boolean | `true`, `false` |
| Operators | `+ - * / % == != < <= > >= =` |
| Punctuation | `( ) [ ] , : .` |
| Range | `..` |
| Layout | `NEWLINE`, `INDENT`, `DEDENT` |

`1..5` is tokenized as integer `1`, `..`, integer `5`. A single `.` that is not part of a float is a `DOT` token (module qualification or, from 0.13.1, field access). `3.14` is still a float.

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
program        ::= { use_stmt } { item }
item           ::= record_decl | function

use_stmt       ::= "use" module_path [ "as" IDENT ] NEWLINE
module_path    ::= IDENT { "/" IDENT }

record_decl    ::= [ "share" ] "record" IDENT ":" NEWLINE INDENT field_decl { field_decl } DEDENT
field_decl     ::= IDENT ":" type_name NEWLINE
type_name      ::= IDENT | IDENT "." IDENT

function_name  ::= IDENT | "repeat"
function       ::= [ "share" ] "create" function_name [ "(" [ IDENT { "," IDENT } ] ")" ] ":" suite

suite          ::= NEWLINE INDENT { statement } DEDENT

statement      ::= assignment
                 | place_assign
                 | call_stmt
                 | say_stmt
                 | give_stmt
                 | if_stmt
                 | repeat_stmt

assignment     ::= IDENT "=" expression NEWLINE
place_assign   ::= place "=" expression NEWLINE
place          ::= primary ( "[" expression "]" | "." IDENT )
                   { "[" expression "]" | "." IDENT }
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
postfix        ::= primary { "[" expression "]" | "." IDENT }
primary        ::= INTEGER | FLOAT | STRING | "true" | "false"
                 | IDENT | call | list | "(" expression ")"
call           ::= callee "(" [ expression { "," expression } ] ")"
callee         ::= IDENT | IDENT "." function_name
list           ::= "[" [ expression { "," expression } ] "]"
```

`place_assign` is index assignment or field assignment. It requires at least one `[]` or `.` so `name = value` stays `assignment`.

Record construction uses the existing `call` form. `Point(1, 2)` and `geom.Point(1, 2)` parse as calls; semantic analysis decides whether the callee is a function or a record type. There is no separate constructor token. A callee is only an unqualified name or `alias.name`. `point.origin.move(1)` is not a call: YUKT has no methods, and constructors are not chained through field access.

`type_name` is an identifier or a qualified name. `integer`, `float`, and `boolean` are builtin type names in this position. They are **not** reserved words. A later identifier in the same file may still be named `integer` as a variable. A **record** must not be named `integer`, `float`, `boolean`, `string`, or `list`.

Operator precedence, highest first:

1. index `[]` and field `.` (postfix, left-to-right: `a.b.c` is `(a.b).c`, `points[i].x` is `(points[i]).x`)
2. unary `-`
3. `* / %`
4. `+ -`
5. `< > <= >=`
6. `== !=`

All binary operators are left-associative. Field access and indexing are postfix operators, not binary `.` at the same level as `+`.

## Features in 0.01

### Variables

```yukt
name = "Ayush"
age = 21
ready = true
items = [1, 2, 3]
```

Assignment is the only name-binding form. There is no `let` or `var`. Type names appear only on `record` fields (0.13.1); function parameters remain names only.

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

`items[i]` reads one element. `items[i] = value` replaces one element. The collection must be a list. The index must be an integer. The first element is at index `0`. From 0.13.1, `point.x = value` assigns a record field; see [Dot resolution](#dot-resolution).

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

YUKT is statically typed. Milestone 0.02 infers types from literals, operators, assignments, and calls. Function parameters and bodies still have no type annotations. **0.13.1 adds type names only on `record` fields.**

| Literal / form | Type |
| --- | --- |
| `21` | integer |
| `3.14` | float |
| `"Hello"` | string |
| `true` / `false` | boolean |
| `[1, 2, 3]` | list[integer] |
| `Point(1, 2)` when `Point` is a record | that record type (nominal) |
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

Record types are **nominal**. Two records with the same field names and field types are different types if they have different declarations. `<` `>` `<=` `>=` are not defined on records. `==` / `!=` on records are specified in [Record equality](#record-equality).

### Inference

- A variable's type is the type of its first assignment. Later assignments must match.
- Assignments bind in the enclosing function, not in `if` / `otherwise` blocks.
- A name may be used only after an assignment appears earlier in that function.
- Parameter types are inferred from uses in the body and from argument types at call sites, including record types.
- If a parameter or empty list never gets a concrete type, the compiler reports `Cannot infer type`.
- All `give` expressions in a function must have the same type. That type is the function's return type. A function may `give` a record.
- A function with no `give` returns void and cannot be used as an expression.
- Record field types are written and are not inferred.

### Scopes

- Function names are global to the file. Duplicate function names are an error.
- Record names are global to the file. Duplicate record names are an error.
- A record name must not collide with a `create` name, a `use` alias, `count`, or `zeros` in that file.
- Variables cannot reuse a function name or a record name.
- `repeat` iterators are scoped to the loop body.
- A loop variable cannot be reassigned and cannot collide with an existing name.

### Statements

- `if` conditions must be boolean. Integers are not treated as true or false.
- `repeat` bounds must be integers.
- `say` accepts any non-void value, including records ([Printing records](#printing-records)).
- Call argument count and types must match the callee. Record constructors are checked the same way: arity and types must match the field list.
- A list index must be an integer. Only lists can be indexed. Record values cannot be indexed.
- Field assignment `place.field = value` requires a record place and a field of that record; the value type must match the field type.
- `count` takes one list. `zeros` takes one integer. `zeros` cannot allocate a list of records.

## Memory

- Integers, floats, and booleans are stack values. LLVM `mem2reg` promotes them when it can.
- 0.13.1 records of scalars and nested records are stack values. Assignment and `give` copy `size` bytes ([Record layout](#record-layout-host-abi)). There is nothing to free. Heap-typed record fields are not part of this slice.
- Lists and strings live on the heap. List payloads are 16-byte aligned. The runtime releases them when `main` returns. Size-class reuse is bounded and not thread-safe; allocating loops stay sequential. Exact bins, slot limits, and retained bytes are in `docs/RUNTIME.md`.
- Function calls use the native C convention after name mangling: `create add` becomes `yukt_add`. Record values passed or returned by value use the host C ABI of that record’s layout.

## Automatic parallelization

The compiler may run a `repeat` loop on several cores when every iteration is independent:

- the body has no `say`, `give`, or user function call
- list writes use an index that mentions the loop iterator (including 0.13.1 field writes through that element, such as `points[i].x = ...`)
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

## Out of scope

The following are **not** part of YUKT 0.13.1 and must not be parsed as if they were:

- classes, inheritance, or methods (`point.greet()` is an error; `.` is module qualification or field access, not `list.len`)
- heap-typed record fields (`string`, `list`, maps) — those wait for 0.13.2
- `ref`, `foreign`, `ok`, `fail`, ownership, and error values
- named-field construction (`Point(x: 1)`)
- recursive records, including indirect cycles
- `and` / `or` / `not`
- bitwise operators
- `for`, `while`, `break`, `continue`
- type annotations on functions, parameters, or locals (field types on `record` are the only annotations)
- an interpreter or bytecode VM
- package versions, remotes, or a package manager
- maps, JSON, and file I/O (stdlib APIs after 0.13.1–0.13.5)

## 0.12 — Modules and imports

**Status: specified.** This section is the source of truth for the module system.

### Purpose

A program is still one native executable. The entry file has `create main`. Other `.yukt` files are modules. A module is a file of functions and, from 0.13.1, record declarations. There is no executable top-level code.

### New reserved words

```
use  share  as
```

These cannot be used as names. 0.13.1 also reserves `record` (see [Reserved words](#reserved-words)).

### Grammar additions

`use` lines come first, then items. A `use` after a `create` or `record` is an error. The complete grammar is at the top of this document. 0.12 added `use` / `share` / qualified calls. 0.13.1 adds `record_decl` as an `item` beside `function`.

`/` in a `use` path is a path separator, not division. `.` between two identifiers in a callee is **module qualification**, not a float and not a method. A single `.` that is not part of a float literal is a token (0.12). `math.sqrt` without `(` is an error unless the left side is a record value (0.13.1 field access). YUKT has no first-class function values. Field vs module resolution is in [Dot resolution](#dot-resolution).

### Visibility

- `create name` is **private** to its source file. Other files cannot call it.
- `share create name` is **exported**. Importers call it as `alias.name(...)`.
- `record Name` is **private** to its source file. Other files cannot name it, construct it, or use it as a field type ([Imported record visibility](#imported-record-visibility)).
- `share record Name` is **exported**. Importers name it `alias.Name` and construct it as `alias.Name(...)`.
- `share` is only valid immediately before `create` or, from 0.13.1, `record`. `share` on any other form is an error.
- `create main` cannot be `share`. `share create main` is an error.
- Only the **entry file** (the path passed to `yukt build` / `yukt run`) may define `create main`. A library module that contains `main` is an error.
- The whole program still has exactly one `create main`, with no parameters.
- There are no module-level variables. Only functions and (from 0.13.1) records are bindable across files.
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
- The alias is a namespace, not a value. It is valid as the left side of `alias.name(...)` (shared function or shared record constructor) and of `alias.Name` in a field `type_name`. An unqualified `alias` in an expression is a local or parameter if one exists; it is not a namespace value. `alias.field` without `(...)` is an error: a namespace is not a record value.
- Two `use` lines in the same file that bind the same alias are an error, even if they resolve to the same file.
- An alias must not collide with a `create` name, a record name in that file, or with `count` / `zeros`.
- Unqualified `name(...)` looks up functions and records in the **current file**, then builtins. It does not search imported modules. `use` is **not transitive**: if A uses B and B uses C, A cannot call C (or construct C’s records) without its own `use`.
- `alias.name` looks up `name` only among **shared** functions and **shared** records of that module. Unknown or private names are errors at the `.` (file, line, column, and a note of what is shared). A private record is not constructible as `alias.Name(...)`.
- Exported function types are inferred from the defining body **and** all call sites in the program (one type context). Conflicting sites are type errors. A **shared** function must not mention a private record in a parameter type or `give` type (inferred or written). That would leak a private type across the module boundary.

### Dependency resolution

Module identity is the canonical file path of a `.yukt` file. Two `use` strings that resolve to the same file are one module.

A module path is one or more identifiers separated by `/`. It must not contain `.`, `..`, a `.yukt` suffix, or an empty component.

Resolve `use p`:

1. If the first component is `std`, the path must have at least two components. `use std` is an error. `use std/math` resolves only to `<stdlib-root>/math.yukt`. `<stdlib-root>` is `$YUKT_STD` if set, else the compile-time `YUKT_STDLIB_DIR`, else `<executable>/../share/yukt/std`. The project tree is **not** searched for `std`. User modules cannot start with `std`.
2. Otherwise, try `<directory of the importing file>/p.yukt`.
3. Otherwise, try `<directory of the entry file>/p.yukt`.
4. Otherwise report `Cannot find module p`, with notes listing the paths that were tried.

Importer-relative (step 2) wins over entry-relative (step 3) when both exist as different files. Module identity is `std::filesystem::canonical` of the resolved file. Two `use` strings that canonicalise to the same path are one module; aliases stay per-importer.

No search of `PATH`, the network, or a package registry. 0.12 has no versions and no package manager.

If the file cannot be opened, that is a diagnostic on the `use` line.

### Compilation

- `yukt build entry.yukt` and `yukt run entry.yukt` load the entry file, then recursively load each used module. `--ast`, `--types`, `--emit-ir`, `--emit-llvm`, and `--explain` do the same. `--tokens` lexes only the named file. `--ast` prints each loaded file.
- Compilation is **whole-program**: one host-native executable. 0.12 does not emit separately linked YUKT libraries.
- The frontend stays LLVM-free. The driver owns the module graph; semantic analysis is whole-program; each function still lowers through YUKT IR.
- Do not concatenate library sources into the entry file as a parse hack.
- LLVM / C names: entry `create main` remains `yukt_main`. Every other function is `yukt_<prefix>$<name>`, where `<prefix>` is the file path relative to the entry directory, without `.yukt`, with `/` replaced by `$` (not `_`, so `a/b_c` and `a_b/c` stay distinct). Standard-library files use `std$` plus the path under `<stdlib-root>` (`std/math.yukt` → `yukt_std$math_add`). After mangling, duplicate LLVM names are an error. A module file that is not under the entry directory and not under `<stdlib-root>` is an error. If the entry file defines any function other than `main`, its file stem must be a YUKT identifier.
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

Every module error has file, line, and column of the `use`, `share`, or qualified call. `SourceLocation` carries a file path. Missing modules, private access (including private records), alias collisions, reserved `std` misuse, and cycles must not be ignored.

### Non-goals for this design

- Selective import (`use add from math`) and glob import (`use math.*`)
- Re-export (`share use math`)
- First-class function values
- Method call (`point.greet()`). Field access is 0.13.1, not 0.12
- Separate compilation to `.o` per module as a user feature
- Package versions, lockfiles, or downloads

## 0.13.1 — Records

**Status: implemented.** This section is the source of truth for records. It is independently releasable: programs that do not use records keep 0.12 meaning. Ownership, `ref`, `foreign`, `ok` / `fail`, and heap-typed fields are **not** in this slice.

0.13.1 records are named product types of **scalars and nested records**. Copying a record copies bits. There is nothing to free.

### Shape

```yukt
record Point:
    x: integer
    y: integer

share record Size:
    w: integer
    h: integer

create main:
    p = Point(1, 2)
    say p.x
    p.y = p.y + 1
```

- Field types are written. Function bodies still infer.
- Allowed field types in **0.13.1**: `integer`, `float`, `boolean`, and other record types in scope (same file, or `alias.Name` for an imported `share record`).
- `string`, `list`, `ref`, maps, and other heap types are **not** valid field types in this slice. `record Person: name: string` is an error: heap fields wait for 0.13.2.
- Record types are **nominal**. `Point` is not `Size` even if both have two integers.
- No inheritance, methods, generics, default field values, packed layout, or named-field construction.
- Zero-field records are an error (no empty product yet).
- A record must have at least one `field_decl`. An empty suite is an error, as for other blocks.
- Duplicate field names in one record are an error.
- Field names follow the reserved-word rules for identifiers.
- A record must not be named `integer`, `float`, `boolean`, `string`, or `list`.

Records are ordinary types. They may appear as locals, parameters, `give` types, list elements (`[Point(1, 2), Point(3, 4)]`), and nested record fields. `zeros(n)` still allocates `list[integer]` only. Indexing a list of records is checked: an out-of-range index traps, like boolean, string, and nested-list indexes.

### Record declaration ordering

Record declarations in one file may refer to records declared **later** in that file.

```yukt
record Box:
    size: Size

record Size:
    w: integer
    h: integer
```

This is valid. The compiler must:

1. Collect every `record` / `share record` in the file (and every imported `share record` reachable from `use`).
2. Resolve each field `type_name` against builtin types and those collected records.
3. Then validate the record-type graph and compute layouts.

Source order of `record` vs `create` does not restrict field types. A function may construct a record declared later in the same file (same collect-then-analyze rule as 0.12 function calls).

### Recursion

**No recursive records**, including indirect cycles. The record-type graph is a DAG.

- `record Node: next: Node` is an error.
- `record A: b: B` plus `record B: a: A` is an error, reported on the **edge that closes the cycle**, with a note listing one cycle as record names.

A record may nest another record that does not path back to it.

### Visibility

Same model as functions.

```yukt
record Point:
    x: integer
    y: integer

share record Size:
    w: integer
    h: integer
```

- `record` is **private** to the source file.
- `share record` is **exported**. Importers name it `alias.Size`.
- `share` is only valid immediately before `record` or `create`.

### Imported record visibility

Construction of an imported record is `geom.Point(1, 2)`, not unqualified `Point`, unless this file also defines `Point`.

A **private** record cannot be used from another module in any of these ways, even if a caller could guess the field types:

- `alias.Name(...)` construction
- `alias.Name` as a field `type_name`
- mentioning it in a `share create` parameter type or `give` type
- mentioning it as a field of a `share record`

```yukt
# geom.yukt
record Hidden:
    x: integer
    y: integer

share record Point:
    x: integer
    y: integer

share create origin:
    give Point(0, 0)
    # give Hidden(0, 0) is an error: private record escaping through share create
```

```yukt
# app.yukt
use geom

create main:
    p = geom.Point(1, 2)
    # geom.Hidden(1, 2) is an error: Hidden is private
    # h = Hidden(1, 2) is an error: no glob import; Hidden is not in this file
```

A `share record` field type must be a builtin 0.13.1 field type or a `share record` (same file or imported). A private record must not appear in a shared record’s fields.

### Construction

Positional only, **declaration order**.

- `Point(1, 2)` is valid when `Point` is a record in this file with two fields of those types.
- Missing fields, extra fields, or a type mismatch: compile error at the constructor, naming the record and the expected fields.
- No named-field construction (`Point(x: 1)`) in 0.13.
- Unqualified `Point(...)` does not search imports.

Boolean fields are stored as specified in [Boolean ABI](#boolean-abi).

### Dot resolution

`.` already means **module qualification** on callees: `math.add(1, 2)` requires `(...)`.

0.13.1 adds **field access** on values: `point.x` has no call parentheses. Resolution uses the **symbol’s actual kind and type**, not spelling:

1. If the left name is a `use` alias in this file, it is a **namespace**. `alias.field` without `(...)` is an error (`math` is a module, not a value). `alias.name(...)` looks up a **shared** function or **shared** record constructor. Unknown or private names error at `.` with a note of what is shared (0.12 rule).
2. If the form is `expr.name` without `(...)` and `expr` has a record type, the right name is a **field**. A missing field names the type and the known fields.
3. If the form is `expr.name(...)` and `expr` is a record value (not a `use` alias), that is a method-call error. No methods in 0.13. `point.add(1)` is this error, not field access.
4. If the left side is a known non-record, non-namespace value, error: `` `.` is field access or a module call, not valid on integer `` (or the actual type).
5. If the left name is unknown, keep the 0.12 `Unknown name` diagnostic. Do not guess “maybe a module.”

Field assignment: `point.x = 3`. Nested places are allowed: `box.size.w = 1`, `points[i].x = 1`. Assigning a nested field mutates that field in place inside the outer record or list element. `origin = widget.origin` copies bits; later `origin.x = 1` does not change `widget`.

No methods (`point.greet()` waits).

### Record equality

`==` and `!=` on records are fieldwise, in declaration order, using the existing per-type equality of each field. They yield boolean. Equality does **not** compare padding bytes.

- Same record type, corresponding fields equal → `==` is true.
- `Point(1, 2) == Point(1, 2)` → true.
- `Point(1, 2) == Point(1, 3)` → false.
- Nested records compare fieldwise recursively.
- `!=` is the negation of `==`.

Float fields use **existing IEEE equality**, including NaN:

- Language `==` on float is ordered equality (`NaN == NaN` is false). A record that contains NaN is therefore not equal to itself.
- Example: if `s` is a `Sample` whose float field is NaN, then `s == s` is false. `0.0` and `-0.0` compare equal, as they do for float `==`.
- Sort’s NaN total order (`-Inf < finite < +Inf < NaN`) is a `std/sort` rule. It does **not** apply to record `==`.

Records of **different types** cannot be compared. That is a type error, not a false result:

```yukt
# Point(1, 2) == Size(1, 2) is an error, even if both have two integers
# Point(1, 2) == 1 is an error
```

This is the same 0.12 rule that both sides of `==` / `!=` must have the same type. Distinct record declarations do not unify.

### Record layout (host ABI)

Field order is declaration order. Layout matches a C struct of the corresponding types on the **host** triple (the only triple 0.12/0.13 compile). Not packed.

| YUKT field | C stand-in | Size | Align |
| --- | --- | --- | --- |
| `integer` | `int64_t` | 8 | 8 |
| `float` | `double` | 8 | 8 |
| `boolean` | `int8_t` 0 or 1 | 1 | 1 |
| nested record | that record’s layout | its size | its align |

Offsets:

1. `offset_0 = 0`
2. `offset_i = align_up(offset_{i-1} + size_{i-1}, align_i)`
3. Record alignment = max of field alignments (at least 1)
4. Record size = `align_up(end of last field, record alignment)`

`align_up(n, a) = (n + a - 1) & ~(a - 1)` for power-of-two `a`.

Examples:

- `Point` with two integers: offsets 0 and 8, size 16, align 8.
- `on: boolean` then `n: integer`: `on` at 0, seven bytes of padding, `n` at 8, size 16, align 8.

This is the layout 0.13.4 will pass to C for scalar records. Changing it later is an ABI break; do not “optimize” padding in the compiler.

Copy is memcpy of `size` bytes. Padding bytes are unspecified; they are copied with the record and ignored by `==`.

### Boolean ABI

Every YUKT boolean **field** occupies one `int8_t` and is **normalized to 0 or 1** on construction and on field assignment:

- `false` is stored as `0`
- `true` is stored as `1`

No other byte value is a valid representation of a boolean field after a YUKT write. SSA booleans in the 0.12 LLVM lowering may still be `i1`; converting `i1` to `i8` `{0,1}` at a record field store is a lowering duty, not a language conversion. List boolean elements already use `int8_t` in the runtime; this slice does not change list layout.

This keeps the C ABI of a boolean field predictable for 0.13.4. It is not implicit conversion between YUKT types.

### Printing records

`say` of a record prints the **declared** record name, then field values in declaration order, in constructor form, with `", "` between fields. Internal type identity stays module-qualified (`prefix$Name`); printing does not use that identity, so two imported `Point` types both print as `Point`:

```text
Point(1, 2)
```

Nested records print recursively. A list of records uses the existing list format with each element printed as a record, for example `[Point(1, 2), Point(3, 4)]`. Booleans print `true` / `false`. Integers and floats use the same formatting as `say` of those types. There is no trailing comma.

### Diagnostics

Every record error has file, line, and column. Required diagnostics:

| Situation | Requirement |
| --- | --- |
| Unknown field | Error at `.`; name the record type and list known fields |
| Module used as a value | `alias.field` without `(...)`; `math` is a namespace |
| Value used as a module | Non-record left of a qualified call, or `.` on a known non-record |
| Constructor arity or type | Error at the call; name expected fields and types |
| Zero-field record | Error on the `record` line |
| Heap field type in 0.13.1 | Error on the field; heap fields wait for 0.13.2 |
| Cycle in record types | Error on the edge that closes the cycle; note one cycle |
| Private record constructed or named from another module | Error at `.` or at the constructor; note that the record is private |
| Private record escaping through `share create` or `share record` fields | Error on the shared declaration |
| `==` / `!=` of different record types | Same-type error; do not compare structurally |
| Methods | `point.add(1)` is not a field and not a call |
| Unknown left name | 0.12 `Unknown name`; do not guess a module |

### Architecture notes (0.12 compiler)

These clarifications do **not** conflict with the 0.12 architecture. They extend it:

- The analyzer already collects every `create` before checking bodies. Record declarations use the same collect-then-validate order so later records can appear in earlier field lists.
- `==` already requires both operands to unify as one type. Distinct record names are distinct types (nominal), so different records cannot be compared.
- LLVM currently materializes SSA booleans as `i1` and list booleans as `int8_t`. Record boolean fields follow the list/C `int8_t` ABI and normalize to 0 or 1 on store. That is a lowering mapping, not a change to language `true` / `false`.
- Private records follow private functions: lookup is per-module, default hidden, `share` exports.

Implementation must not mix record layout with heap drop. 0.13.1 has no destructors and does not call `yukt_release` for records.

### Regression gate (0.13.1)

This slice is implemented. The gate is:

- Entire 0.12 CTest suite green, including `tests/stdlib/`.
- New tests: private vs `share record`; import `alias.Point(...)`; rejection of `alias.Hidden(...)`; field get/set; constructor arity/types; later-declared nested records; cycle rejection; `.` on integer; `.` on a module without `(...)`; `Point(1, 2) == Point(1, 2)`; NaN field `==`; `Point == Size` type error; boolean field stored as 0 or 1; `std/list.fill` on `list[record]` is a type mismatch and must not emit LLVM.
- `file` of a native binary still matches the host architecture.
- No change to allocator caps, 0.11 benches, or `v0.12.1` behavior of programs that contain no records.



## 0.13.2 — Ownership

Status: specified for implementation on branch `yukt-0.13.2-ownership`. `v0.13.1` exists locally (commit `41f2e04`). 0.13.3 (references) has not started. This section is complete enough to implement without reading `docs/YUKT_013_DESIGN.md`. It adds no keywords, operators, or statements. Programs that use no heap-typed names keep their 0.13.1 meaning; scalar-record programs (records of only `integer`, `float`, `boolean`, and nested plain records) are **unchanged**: bitwise copy, no drop, no tracking.

### What is owned

A **heap type** is `string`, `list[T]`, or a record that **contains ownership**. A record contains ownership iff any field's type is `string`, `list[T]`, or a record that contains ownership. Every heap allocation has **exactly one owner** at a time.

**Plain values** are `integer`, `float`, `boolean`, and records of only those (including nested plain records). Assignment copies bits. They are not in the ownership state machine and are never dropped.

A record that contains ownership owns its heap fields. Moving the record moves every owned field together. Heap fields in records are allowed from 0.13.2 on; in 0.13.1 they were an error.

```yukt
record Profile:
    name: string
    scores: list
```

Field types stay `type_name` (an identifier or `alias.Name`); the grammar is unchanged. In 0.13.2 the field type names `string` and `list` become valid. A field written `list` is `list[E]`. There is no `list[E]` field syntax. `E` is inferred by unification from the constructor arguments and uses of that record type, and must be the same everywhere. If constructors disagree on `E`, or `E` cannot be inferred, that is a type error at the offending constructor or the field. Non-integer element types (for example `list[string]` or `list[Point]`) are allowed.

### Per-name states

Ownership is tracked per name for **heap-typed names only**: locals, parameters, and record fields whose type is a heap type. Bindings stay function-scoped.

| State | May read | May overwrite | May move out | Meaning |
| --- | --- | --- | --- | --- |
| **Uninit** | no | yes (first assign) | no | Name declared or not yet assigned; no allocation |
| **Owned** | yes | yes (drops previous) | yes | Name owns exactly one live allocation |
| **Moved** | no | no | no | Name no longer owns an allocation |

(`Borrowed` belongs to 0.13.3 and does not exist here. In 0.13.2 a borrow lasts only for one statement or expression and the owner stays Owned.)

**Moved-from is dead.** A Moved name cannot be read, indexed, passed, `give`n, assigned through (`moved[0] = 1`), moved again, or **reassigned**. Reassigning a Moved name (`s = zeros(3)` after `s` was moved) is an error in 0.13.2. Re-initialization needs a later, explicit spec.

### Transitions

```
Uninit --assign heap value-->   Owned
Owned  --x = y (y a heap name)--> drop old x if Owned; x takes y's allocation; y becomes Moved; x becomes Owned
Owned  --x = e (e fresh value)--> drop(x); then x owns e
Owned  --give x-->              x becomes Moved; the caller receives an Owned value; no drop in this function
Owned  --end of function-->     drop(x) on every return path, unless x was given
Owned  --pass x to user call-->  x becomes Moved (callee parameter is Owned)
Owned  --builtin borrow-->      stays Owned

Moved  --any use-->             compile error: Use after move
```

- **Assign.** First assignment of a heap value to an Uninit name makes it Owned.
- **Move.** `x = y` for heap names is a pointer transfer. No runtime call is made on the move itself.
- **Overwrite drop.** Assigning to an Owned name drops its previous allocation first, then stores the new value.
- **`give` transfer.** `give x` transfers the allocation to the caller. The callee must not drop it. The caller of a function that `give`s a heap value receives an Owned value. A function that does not `give` an owned local drops it on every return path.
- **End-of-function drop.** Every Owned heap name, including parameters, is dropped when the function returns unless it was moved or given.
- **User-call move.** Passing an owned heap value **by value** to a user function moves it into that parameter. The callee owns it and drops it unless it `give`s it back.

### Nested records

- **No field move-out.** `s = profile.scores` where the field type is a heap type is an error (`Cannot move out of record field`). Use `list.copy(profile.scores)` to obtain a distinct owner. `list.copy` on a field is a read: the field stays Owned and the record stays Owned.
- **Field overwrite drops.** `profile.scores = zeros(n)` evaluates the right-hand side first, then drops the previous list, then stores the new one — the same order as local assignment. `p.scores = zeros(count(p.scores))` is valid.
- **Whole-record move.** Moving `profile` moves `name` and `scores` together. One drop of `profile` drops every owned field. Fields are never dropped separately after a whole-record move.
- **No partial moves.** Records with some fields Moved and some Owned do not exist in 0.13.2.
- Using a moved record, or any of its fields, is use after move.

### List element rule

A list of heap elements (`list[string]`, `list[list[T]]`, `list[record]` with owned fields) owns its slots.

- `items[i]` is a **borrow**: the list stays Owned.
- `items[i] = v` drops the previous slot, then moves `v` in.
- Binding a heap element out of a list (`s = items[0]` where the element type is a heap type) is **not a steal**. It is a compile error in 0.13.2: `Cannot move out of list element`. For scalar element types `s = items[0]` still reads a value.
- To duplicate, use `list.copy` on the whole list. This matches the record no-field-move-out rule.
- Dropping a list first drops its owned elements.

### If-join

At an `if` / `otherwise`, snapshot ownership before the branch. Analyze `then` from the snapshot and record its result. Restore the snapshot, analyze `otherwise` (an absent `otherwise` leaves the snapshot), and record its result. Join per name:

- if any path leaves the name **Moved**, the join is **Moved**;
- else if any path leaves it **Owned**, the join is **Owned**;
- else **Uninit**.

Using a Moved name after the `if` is use after move.

### Repeat

Moving or dropping a heap name that lives **outside** the loop from inside a `repeat` body is an error (`Cannot move inside repeat`): the first iteration would leave later iterations with a moved value. Assigning a **new, loop-local** heap name each iteration is allowed; it is dropped at the end of that iteration (and on overwrite). After the loop that name is **Moved**; reading it is `Use after move`. The iterator remains an integer.

### String literals

A string literal that becomes an owned value (bound to a name, passed to a user function, or stored into a list or record field) is **copied** into a tracked buffer. Dropping that name releases the copy. The literal's static storage is never freed.

### Builtin borrow versus move

| Form | Effect on owner |
| --- | --- |
| `say x`, `count(x)`, `x[i]`, `x[i] = v` (the index itself) | borrow; owner stays Owned |
| `zeros`, list literals, string literals, `str.*` that allocate | produce a fresh Owned value |
| `list.copy(x)`, `list.slice(x, …)` | borrow `x`; return a new Owned value; owned slots are cloned |
| `list.fill`, `list.reverse`, `sort.*` | borrow the destination; do not move it |
| user `create f(items):` with a heap parameter | **move** into `f` |
| `str.len` / `at` / `find` / `contains` / `starts` / `ends` | borrow |
| `str.concat` / `slice` / `repeat` / `join` | borrow inputs; return a new Owned string |
| `str.split` | borrow input; return a new Owned list that owns the parts |

`list.fill` of `list[record]` stays a type mismatch (0.13.1 rule).

### Runtime contract

`yukt_release(ptr)` unregisters **one** live tracked allocation, then returns its payload to the size-class cache or `free`s it. Releasing a list drops its owned elements first. Releasing null is a no-op; releasing a pointer that is not registered, or releasing the same live object twice, traps (compiler bug). The compiler inserts the call; there is no user `free` and no destructor syntax.

`yukt_runtime_release` still runs when `yukt_main` returns and releases anything still registered. It is a **safety net**, not a second owner: a correct 0.13.2 program releases or transfers everything it owns. Allocator caps are unchanged: six size classes, four slots per class. Parallelization rules are unchanged; a parallel `repeat` still refuses bodies that allocate. See `docs/RUNTIME.md`.

### Diagnostics

Every ownership error has file, line, and column.

| Situation | Title |
| --- | --- |
| Read, index, pass, `give`, assign through, or move a Moved name | `Use after move` |
| Reassign a Moved name | `Use after move` (reassignment is not allowed in 0.13.2) |
| Move or drop an outer heap name inside `repeat` | `Cannot move inside repeat` |
| Bind a heap element out of a list | `Cannot move out of list element` |
| Bind a heap field out of a record | `Cannot move out of record field` |

### What 0.13.2 does not do

- No `ref`, no borrow checker (0.13.3). No `foreign` (0.13.4). No error values (0.13.5). No stdlib expansion (0.13.6).
- No block-scoped locals, no `free` in user code, no destructor syntax, no GC.
- No re-initialization of Moved names.

### Regression gate (0.13.2)

- Entire 0.12 and 0.13.1 CTest suites stay green.
- Use after move is rejected; use after move of a whole record is rejected.
- `give` of a list does not free before the caller reads it.
- Overwrite drops the previous allocation (observable by live-allocation count).
- Moving a `Profile`-style record does not double-free; overwriting one field drops only that field.
- Allocator caps and parallel thresholds unchanged; `benchmarks/results-0.11.json` unchanged.
