# YUKT 0.18 design — Developer experience

Status: **0.18.1 implemented** after macOS arm64 and Ubuntu x86-64 CI ([41332352061](https://github.com/Ayushman88/yukt/actions/runs/41332352061), 2026-11-01). 0.18.2 path/git dependencies remain specified, not implemented. 0.18.3, publish, formatter, LSP, and REPL stay out.

0.18 adds **no new language syntax**. No new reserved words. No compiler backend redesign. No new concurrency or networking. Mutex stays specified and deferred (0.17). Windows, CUDA / GPU, and 0.19 stay out.

**Read first:** `docs/LANGUAGE_SPEC.md` (0.16–0.17 language; 0.18.1 tooling); `docs/STDLIB_SPEC.md`; `docs/ROADMAP.md`. File CLI: `yukt build <file.yukt>`, `yukt run <file.yukt> [args]`. Project CLI: `yukt new`, no-file `build` / `run` / `test`.

---

## 0. What 0.18 is

A developer installs YUKT, creates a project, builds it, tests it, and runs it **without manually configuring the compiler**. That is the proof.

Independently releasable in slices:

| Slice | Ships | Must not require |
| --- | --- | --- |
| **0.18.1 project workflow** | Install, `yukt new`, manifest, `yukt build` / `run` / `test` | Dependencies, a registry, formatter, LSP, REPL |
| **0.18.2 path/git deps** | Dependency declarations, version constraints, lockfile, reproducible fetch | A public index or `yukt publish` |
| **0.18.3 thin index** | Public URL → versioned tarball or git tag | A full registry, accounts, or publish |
| **Later (not a 0.18.1–0.18.3 ship-gate)** | `yukt publish`, application packaging polish, docs site, `yukt fmt`, language server, REPL | Windows, CUDA, new syntax |

Single-file `yukt build file.yukt` and `yukt run file.yukt` remain valid forever. 0.18 does not take them away.

---

## 1. Outcome

A completely fresh macOS arm64 or Linux x86-64 machine:

```text
yukt --version

yukt new hello --type cli
cd hello
yukt build
yukt test
yukt run

yukt new api --type server
cd api
yukt build
yukt run
```

Command spelling above is **this design’s proposed surface**. Accepting this file freezes it for 0.18.1 unless a review note changes a name.

---

## 2. Freeze

Do not implement in 0.18:

- New language syntax, keywords, or types
- Compiler backend redesign, second IR, or a new scheduler
- Mutex, TLS, UDP, HTTP/2, keepalive, `https` success, `http.post`
- Windows target, cross-compilation, CUDA / GPU
- A registry product (accounts, search UI, social features)
- `yukt publish` before 0.18.3 is accepted as done
- Formatter, LSP, REPL before 0.18.1 is both-host green

Do not weaken 0.13–0.17 ownership, `result`, `start` / `wait`, D1–D11, or allocator caps.

Hosts: **macOS arm64 and Linux x86-64 only.**

---

## 3. One `yukt` binary

Approach 1, accepted 2026-11-01: one project-aware `yukt`. Not a second `yukt-pkg` tool.

| Invocation | Behavior |
| --- | --- |
| `yukt build <file.yukt>` / `yukt run <file.yukt>` | Today’s file workflow. No manifest required. |
| `yukt build` / `yukt run` / `yukt test` with no file | Require `yukt.toml` in this directory or an ancestor. Use `entry`. |
| Explicit file **and** a manifest | The explicit file wins. Manifest is ignored for that invocation except later for deps (0.18.2). |
| `yukt --tokens` / `--ast` / `--types` / `--emit-ir` / `--emit-llvm` / `--explain` | Unchanged; still take a file. |

`yukt --version` and `yukt --help` stay. `--help` lists the new subcommands once they exist.

---

## 4. 0.18.1 — Install

Official **versioned tarball** (macOS arm64, Linux x86-64) plus an install script.

The tarball contains:

- `yukt` executable
- `std/` tree the compiler already searches
- runtime library the linker already uses

After install, `yukt --version` prints the same version string the compiler already has. No LLVM knowledge is required of the application developer.

Contributor path remains CMake + Ninja + LLVM, as today. Homebrew/apt may wrap the tarball later; they are not the 0.18.1 ship-gate.

Upgrade: installing a newer tarball replaces `yukt`, `std/`, and the runtime. Projects are source + lockfile (0.18.2); they are not stored inside the toolchain directory.

Failure: missing LLVM **on the contributor path** stays a CMake error. The tarball install must not tell the application developer to install LLVM.

---

## 5. 0.18.1 — Project creation and manifest

```text
yukt new <name> --type cli
yukt new <name> --type server
```

Creates `<name>/` in the current directory. Refuses if `<name>` already exists.

Layout:

```text
<name>/
  yukt.toml
  src/main.yukt
  tests/main.yukt
  tests/main.yukt.out    # optional; created for the CLI template
```

`yukt.toml` is a **small handwritten TOML subset**. No new parser library. Keys in 0.18.1:

```toml
name = "hello"
version = "0.1.0"
type = "cli"          # or "server"
entry = "src/main.yukt"
```

Unknown keys are errors. Duplicate keys are errors. `name` is a YUKT identifier (`[A-Za-z_][A-Za-z0-9_]*`). `version` is `major.minor.patch` digits. `type` is `cli` or `server`. `entry` is a path relative to the manifest, must exist, must end in `.yukt`.

No `[dependencies]` table in 0.18.1. If present, error: dependencies are 0.18.2.

---

## 6. 0.18.1 — Build, run, test

Output directory: `<project>/build/`. `yukt build` writes the executable there (`build/<name>`). `yukt run` builds if needed and runs it; extra args after `yukt run` become program argv (same as today’s `yukt run file.yukt [args]`).

Diagnostics stay file:line:column. A missing manifest for a no-file `build` / `run` / `test` is a title such as `Cannot find project` with a note to run `yukt new` or pass a `.yukt` file.

### `yukt test`

Discover `tests/**/*.yukt` under the project root (sorted by path). For each file:

1. Compile and run (same native pipeline as `yukt run`).
2. Non-zero process exit → fail.
3. If `<file>.out` exists next to it, stdout must equal that file’s bytes. Missing `.out` means stdout is not checked.
4. Stderr from the program is not compared unless a later spec says so. Compiler diagnostics on a failing compile fail the test.

Print one line per test: pass or fail, path, and on fail the mismatch or exit code. Exit 0 iff every test passed. Zero test files is a failure (`No tests found`).

No new assert syntax. Tests are ordinary YUKT programs.

---

## 7. 0.18.1 — Templates

Templates use **only** implemented 0.16 / 0.17 APIs.

**CLI (`--type cli`).** `src/main.yukt` reads `io.args` / `io.program`, may read a file with `std/file`, writes with `say`, builds a native executable. `tests/main.yukt` is a program that exits 0; `tests/main.yukt.out` may be empty or a known `say` line from a deterministic `main`.

**HTTP server (`--type server`).** `src/main.yukt` uses `std/http` and `std/json` as needed, listens on `127.0.0.1:0` or a documented port. `yukt run` for this template may block on `http.serve` — that is correct server behavior. `yukt test` for the server template must **not** hang: tests call `http.get` against a `start`ed `http.serve_on` and `give` the serve task (0.17.1 completing shape), or they are pure functions with no serve loop.

Do not invent `http.post`, TLS, or extra framework types.

---

## 8. 0.18.2 — Path and git dependencies

Not implemented in 0.18.1. Specified so 0.18.1 does not paint the corner.

```toml
[dependencies]
foo = { path = "../foo" }
bar = { git = "https://example.com/bar.git", tag = "v1.2.0" }
```

- Path deps are local project roots with their own `yukt.toml`.
- Git deps fetch a tag or commit into a project-local cache and lock that identity.
- `yukt.toml` plus `yukt.lock` (generated) are enough to rebuild bit-for-bit on the same host OS/arch.
- Version constraints are exact tags/commits in 0.18.2; ranges wait for 0.18.3 if needed.
- Cycles are errors. Missing `entry` in a dep is an error.

Resolution does not consult a registry in 0.18.2.

---

## 9. 0.18.3 — Thin public index

A file or URL that maps `name` + `version` to a git tag or tarball URL. `yukt` fetches that artifact the same way as 0.18.2 git/path.

This is **not** a package website, search engine, or account system.

`yukt publish` is later: a developer uploads a versioned tarball or tags git. Not a 0.18.1–0.18.3 ship-gate.

---

## 10. P1 / P2 (after 0.18.1)

| Priority | Capability | Notes |
| --- | --- | --- |
| P1 | Distributable executable | `yukt build` already emits a host binary. P1 is documenting copy-the-binary and any rpath/stdlib layout the tarball install needs. |
| P1 | Docs and examples | Starter repos matching the two templates; install → new → test → run on both hosts. |
| P2 | `yukt fmt` | After 0.18.1 is stable. |
| P2 | Language server | After 0.18.1 is stable. |
| P2 | REPL | After 0.18.1 is stable. |

---

## 11. What 0.18.1 must not change

- Frontend independence from LLVM
- Native `yukt build` / `yukt run` meaning (host executable, not an interpreter)
- Stdlib modules and language law in `docs/LANGUAGE_SPEC.md` / `docs/STDLIB_SPEC.md`
- Frozen benches (`benchmarks/results-0.11.json`, `docs/BENCHMARKS.md`, `benchmarks/results-0.15.macos-arm64.json`)
- Allocator cap 8,948,352 bytes

---

## 12. Acceptance (0.18.1)

On a machine that only ran the install script (no CMake):

1. `yukt --version` exits 0 and prints a version.
2. `yukt new hello --type cli` creates the layout in §5.
3. `cd hello && yukt build` writes `build/hello`.
4. `yukt test` exits 0.
5. `yukt run` exits 0.
6. Repeat 2–5 for `--type server`, except `yukt run` may be checked with a bounded test rather than an infinite serve in CI.
7. `yukt build src/main.yukt` still works (file form).
8. Both hosts green.

0.18.2 and 0.18.3 add their own gates after 0.18.1 is accepted as implemented.

---

## Out of this document’s remaining implementation authority

0.18.2 path/git dependencies, lockfile, 0.18.3 thin index, `yukt publish`, formatter, LSP, and REPL wait for their own accepted plans. Do not implement them from this file.
