# YUKT Language Specification

Version: 0.11 (frozen)  
Status: syntax, type checking, native host executables, optimizations, automatic parallelization (including simple +/* reductions), SIMD, execution explanation, and host-native CI. Numerical semantics for integer overflow and float association are defined in this document.

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
- Lists and strings live on the heap. List payloads are 16-byte aligned. The runtime releases them when `main` returns. Size-class reuse is bounded and not thread-safe; allocating loops stay sequential.
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

- **Integer overflow** is **undefined**, sequential or parallel. LLVM lowering uses `nsw` add/mul. Do not rely on wraparound.
- **Integer `+` and `*` reductions** that do not overflow are associative. Parallel and sequential results match.
- **Floating-point `+` and `*` are not associative.** A parallel float reduction may use a different association than a left-to-right sequential fold, and may differ across CPU counts. LLVM vectorization of a sequential float loop may also change association. YUKT does not promise bitwise-identical float reduction results.
- **Division** of integers truncates toward zero. Division by zero is undefined.

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

The following are **not** part of YUKT 0.11 and must not be parsed as if they were:

- modules and imports
- classes or structs
- `and` / `or` / `not`
- bitwise operators
- `for`, `while`, `break`, `continue`
- type annotations
- methods or member access (`.`)
- an interpreter or bytecode VM
