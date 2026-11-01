# 0.18.1 Project Workflow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a self-contained YUKT install plus `yukt new` / project `build` / `run` / `test` on macOS arm64 and Linux x86-64, without new language syntax or a package manager.

**Architecture:** One `yukt` binary. File-based `yukt build <file.yukt>` stays. No-file `build` / `run` / `test` read `yukt.toml`. The compiler already emits native objects and links `libyukt_runtime.a` with host `clang`; 0.18.1 adds project discovery, a tiny TOML subset, templates, prefix-relative runtime/stdlib lookup, and a tarball whose `bin/yukt` resolves LLVM shared libraries from `$PREFIX/lib`. Host `clang` (macOS Command Line Tools or distro `clang`) remains the **user-program linker**. That is not an LLVM distribution.

**Tech Stack:** Existing handwritten C++20 compiler (`yukt_compiler` + `yukt_backend`), LLVM as a bundled shared-library dependency of `yukt` itself, C11 `libyukt_runtime.a`, POSIX filesystem. No new parser libraries. No Windows. No CUDA.

## Global Constraints

- Spec: `docs/YUKT_018_DESIGN.md` (accepted 2026-11-01). Do not implement until this plan is accepted and coding is authorized.
- **0.18.1 only.** No `[dependencies]`, no lockfile, no git fetch, no index, no `yukt publish`, no `yukt fmt`, no LSP, no REPL.
- No new language syntax, keywords, or types. Frontend stays LLVM-free (`compiler/lexer`, `parser`, `ast`, `semantic`, `driver`, `common`, `ir`).
- Do not redesign the backend, IR, or scheduler. Do not change 0.13–0.17 language law, D1–D11, or allocator cap `8948352`.
- Do not overwrite `benchmarks/results-0.11.json`, `docs/BENCHMARKS.md`, `benchmarks/results-0.15.macos-arm64.json`.
- Hosts: macOS arm64 and Linux x86-64. No Windows.
- GCC `-Werror=missing-field-initializers`: field assignment, not designated initializers.
- Diagnostics: title + detail + suggestion, file:line:column when a source file exists.
- Flip docs to 0.18.1 implemented **only** after both-host green. Do not mark 0.18.2 implemented.
- Contributor CMake+LLVM path stays. Tarball path must not tell the application developer to install LLVM.
- Commits only when the user authorizes them. Each task lists a message; do not run `git commit` until authorized.

## File map

| Path | Responsibility |
| --- | --- |
| `compiler/driver/paths.hpp` / `paths.cpp` | Running `yukt` path, install prefix, stdlib, runtime `.a`, templates |
| `compiler/driver/manifest.hpp` / `manifest.cpp` | TOML subset, ancestor search, entry path safety |
| `compiler/driver/project.hpp` / `project.cpp` | `yukt new`, `yukt test` |
| `compiler/driver/module.cpp` | `default_stdlib_root()` delegates to `stdlib_root()` |
| `compiler/backend/pipeline.cpp` | Runtime `.a` via `runtime_archive()`; create `build/` |
| `compiler/backend/llvm_backend.cpp` | Missing-clang diagnostic: `Cannot find linker`, never “install LLVM” |
| `compiler/main.cpp` | `new` / `test` / no-file `build`/`run`; keep file form |
| `share/yukt/templates/cli/` | CLI template |
| `share/yukt/templates/server/` | HTTP server template |
| `scripts/package.sh` | Stage prefix, bundle LLVM dylibs, tar, smoke |
| `scripts/install.sh` | Staging install, backup, rollback |
| `CMakeLists.txt` | New sources, `install()`, `INSTALL_RPATH`, `yukt_project_tests` |
| `tests/project/project_tests.cpp` | Focused CLI + manifest tests |
| `tests/project/cmd.hpp` | Spawn `YUKT_EXECUTABLE` helper |

## Installer and packaging strategy

Today `yukt` dynamically links LLVM with an rpath into Homebrew/distro LLVM, and `YUKT_RUNTIME_LIB` is an absolute build-tree path. A fresh machine cannot run that binary, and a copied `yukt` cannot find `libyukt_runtime.a`.

**Prefix layout** (tarball and `cmake --install`):

```text
$PREFIX/
  bin/yukt
  lib/libyukt_runtime.a
  lib/<LLVM .dylib/.so that bin/yukt actually links, plus their LLVM transitive deps>
  share/yukt/std/            # copy of repo `std/`
  share/yukt/templates/      # cli + server
```

**`yukt` the compiler process** must start with no brew/apt LLVM. After `cmake --install`, `bin/yukt` has `INSTALL_RPATH` `@loader_path/../lib` (macOS) or `$ORIGIN/../lib` (Linux). `scripts/package.sh` copies LLVM shared libraries into `$PREFIX/lib` and rewrites install names / `DT_NEEDED` so resolution is rpath-only. Do not ship `llc`, `opt`, LLVM `clang`, headers, or `llvm-config`.

Contributor `./build/yukt` keeps the current brew/distro **build** rpath so `ctest` still works.

**User programs** `yukt build` produces still link with host `clang` + `$PREFIX/lib/libyukt_runtime.a` + `-lm` (`-pthread` on Linux). If `clang` is missing: title `Cannot find linker`; suggestion is Command Line Tools / distro `clang`, **not** LLVM.

**Stdlib:** `YUKT_STD` if set, else `$PREFIX/share/yukt/std` (`exe/../share/yukt/std`). Contributor builds still define `YUKT_STDLIB_DIR` to the source `std/` so `ctest` does not need the prefix.

**Runtime `.a`:** if compile-time `YUKT_RUNTIME_LIB` exists (contributor), use it; else `$PREFIX/lib/libyukt_runtime.a`.

**Install script:** default `PREFIX=$HOME/.yukt`. Unpack into `$PREFIX.stage.$$`, run `$PREFIX.stage.$$/bin/yukt --version`, then:

1. If `$PREFIX` exists, `mv "$PREFIX" "$PREFIX.bak.$$"`.
2. `mv "$PREFIX.stage.$$" "$PREFIX"`.
3. On any failure after the backup: `rm -rf "$PREFIX"` if the new dir is broken, `mv "$PREFIX.bak.$$" "$PREFIX"`.
4. On success: `rm -rf "$PREFIX.bak.$$"`.

Print `export PATH="$PREFIX/bin:$PATH"`. Never tell the user to `brew install llvm`.

**`yukt new` rollback:** refuse if dest exists; write `$dest.stage.$$` then `rename`; delete staging on failure.

**Manifest errors:** emit diagnostics, do not create `build/`.

## CLI stages

```text
1 prefix paths → 2 manifest safety → 3 yukt new
    → 4 project build/run → 5 yukt test → 6 real templates
    → 7 tarball + LLVM bundle + install.sh → 8 both-host gate + docs
```

Do not start Task N+1 until Task N’s focused tests pass.

## Failure titles (frozen)

| Situation | Title |
| --- | --- |
| No `yukt.toml` for no-file build/run/test | `Cannot find project` |
| Malformed TOML, unknown/duplicate key, `[dependencies]`, bad `entry` | `Invalid manifest` |
| `yukt new` dest exists | `Cannot create project` |
| Bad `name` | `Invalid project name` |
| Missing/invalid `--type` | `Invalid project type` |
| Zero `tests/**/*.yukt` | `No tests found` |
| Host `clang` missing | `Cannot find linker` |
| Runtime `.a` missing after prefix lookup | `Missing runtime` (existing title) |

---

### Task 1: Prefix paths and runtime discovery

**Files:**

- Create: `compiler/driver/paths.hpp`
- Create: `compiler/driver/paths.cpp`
- Modify: `CMakeLists.txt` — add both files to `add_library(yukt_compiler ...)` after `compiler/driver/module.cpp`; add `yukt_project_tests` (copy `yukt_io_tests` block, source `tests/project/project_tests.cpp`, define `YUKT_EXECUTABLE` and `YUKT_TEMPLATES_DIR="${CMAKE_SOURCE_DIR}/share/yukt/templates"`).
- Modify: `compiler/driver/module.cpp` — `default_stdlib_root()` returns `stdlib_root().string()`.
- Modify: `compiler/backend/pipeline.cpp` — `runtime_path()` returns `runtime_archive().string()`.
- Create: `tests/project/project_tests.cpp`
- Create: `tests/project/cmd.hpp`

**Interfaces:**

```cpp
namespace yukt {
std::filesystem::path executable_path();
std::filesystem::path install_prefix();   // executable_path().parent_path().parent_path()
std::filesystem::path stdlib_root();      // YUKT_STD, else YUKT_STDLIB_DIR, else prefix/share/yukt/std
std::filesystem::path runtime_archive();  // existing YUKT_RUNTIME_LIB if that file exists, else prefix/lib/libyukt_runtime.a
std::filesystem::path templates_root();   // YUKT_TEMPLATES, else YUKT_TEMPLATES_DIR, else prefix/share/yukt/templates
}
```

Move the `__APPLE__` `_NSGetExecutablePath` / Linux `/proc/self/exe` block from `default_stdlib_root` into `executable_path()`. Keep `YUKT_STDLIB_DIR` behavior so contributor `ctest` is unchanged.

- [ ] **Step 1: Write failing tests**

`tests/project/cmd.hpp`:

```cpp
#pragma once
#include <filesystem>
#include <string>
#include <vector>

struct CmdResult {
    int status = 1;
    std::string out;
    std::string err;
};

CmdResult run_cmd(const std::vector<std::string>& argv, const std::filesystem::path& cwd);
```

Implement `run_cmd` in `project_tests.cpp` with `posix_spawn`, cwd via `posix_spawn_file_actions_addchdir_np` on macOS; on Linux, `posix_spawn` after `fork` is enough if you `chdir` in the child. Simpler portable version: write a tiny wrapper that `chdir`s in a `fork`ed child, `dup2` stdout/stderr to pipes, `execv(YUKT_EXECUTABLE)`. Match `run_executable` in `compiler/backend/llvm_backend.cpp`.

`tests/project/project_tests.cpp` (Task 1 body):

```cpp
#include "driver/paths.hpp"
#include "test_harness.hpp"

#include <cstdlib>
#include <filesystem>

int main() {
    {
        ::setenv("YUKT_STD", "/tmp/yukt-empty-std", 1);
        REQUIRE(yukt::stdlib_root() == std::filesystem::path("/tmp/yukt-empty-std"));
        ::unsetenv("YUKT_STD");
    }
    {
        const auto exe = yukt::executable_path();
        REQUIRE(!exe.empty());
        REQUIRE(std::filesystem::exists(exe));
        REQUIRE(yukt::install_prefix() == exe.parent_path().parent_path());
    }
    {
        const auto runtime = yukt::runtime_archive();
        REQUIRE(!runtime.empty());
        REQUIRE(std::filesystem::exists(runtime));
        REQUIRE(runtime.filename() == "libyukt_runtime.a");
    }
    return yukt::test::summary("yukt_project_tests");
}
```

CMake (after `yukt_io_tests`):

```cmake
add_executable(yukt_project_tests tests/project/project_tests.cpp)
target_link_libraries(yukt_project_tests PRIVATE yukt_backend yukt_runtime)
target_include_directories(yukt_project_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_definitions(yukt_project_tests PRIVATE
    YUKT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    YUKT_EXECUTABLE="$<TARGET_FILE:yukt>"
    YUKT_RUNTIME_LIB="$<TARGET_FILE:yukt_runtime>"
    YUKT_TEMPLATES_DIR="${CMAKE_SOURCE_DIR}/share/yukt/templates"
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(yukt_project_tests PRIVATE -fno-rtti)
endif()
if(APPLE AND DEFINED LLVM_LIBRARY_DIR)
    target_link_options(yukt_project_tests PRIVATE "-Wl,-rpath,${LLVM_LIBRARY_DIR}")
endif()
yukt_set_warnings(yukt_project_tests)
add_test(NAME yukt_project_tests COMMAND yukt_project_tests)
add_dependencies(yukt_project_tests yukt)
```

- [ ] **Step 2: Run — expect FAIL**

```bash
cmake --build build --target yukt_project_tests
```

Expected: compile error, `driver/paths.hpp` not found.

- [ ] **Step 3: Implement**

`compiler/driver/paths.hpp` as the interface above.

`paths.cpp`: copy executable resolution from `module.cpp` lines 199–214. `stdlib_root()` is the current `default_stdlib_root` logic using `install_prefix() / "share" / "yukt" / "std"`.

```cpp
std::filesystem::path runtime_archive() {
    const std::string configured = YUKT_RUNTIME_LIB;
    if (!configured.empty() && std::filesystem::exists(configured)) {
        return configured;
    }
    const auto candidate = install_prefix() / "lib" / "libyukt_runtime.a";
    if (std::filesystem::exists(candidate)) {
        return candidate;
    }
    return {};
}
```

`YUKT_RUNTIME_LIB` is defined on `yukt_backend` and `yukt`, not `yukt_compiler`. Define it on `yukt_compiler` as `""` default, **or** keep `runtime_archive()` in `pipeline.cpp` calling a paths helper that does not need the macro:

```cpp
// paths.cpp — no compile-time .a
std::filesystem::path prefix_runtime_archive() {
    return install_prefix() / "lib" / "libyukt_runtime.a";
}

// pipeline.cpp
std::string runtime_path() {
    const std::string configured = YUKT_RUNTIME_LIB;
    if (!configured.empty() && std::filesystem::exists(configured)) {
        return configured;
    }
    const auto candidate = prefix_runtime_archive();
    if (std::filesystem::exists(candidate)) {
        return candidate.string();
    }
    return {};
}
```

Use this split so `yukt_compiler` stays free of the backend define. Export `prefix_runtime_archive()` from `paths.hpp`. Tests for Task 1 check `executable_path`, `install_prefix`, `stdlib_root` (`YUKT_STD`), and `prefix_runtime_archive()` filename. Existence of the contributor `.a` stays `pipeline.cpp` / later package smoke.

`default_stdlib_root()`:

```cpp
std::string default_stdlib_root() {
    return stdlib_root().string();
}
```

- [ ] **Step 4: Run tests**

```bash
ctest --test-dir build -R 'yukt_project_tests|yukt_io_tests|hello_run' --output-on-failure
```

Expected: PASS. Contributor runtime path still found.

- [ ] **Step 5: Commit if authorized**

```bash
git add compiler/driver/paths.hpp compiler/driver/paths.cpp compiler/driver/module.cpp \
        compiler/backend/pipeline.cpp CMakeLists.txt tests/project/project_tests.cpp tests/project/cmd.hpp
git commit -m "$(cat <<'EOF'
Resolve YUKT stdlib and runtime archives from the install prefix.

EOF
)"
```

---

### Task 2: Manifest parser and project safety

**Files:**

- Create: `compiler/driver/manifest.hpp`
- Create: `compiler/driver/manifest.cpp`
- Modify: `CMakeLists.txt` — add the two files to `yukt_compiler`
- Modify: `tests/project/project_tests.cpp`

**Interfaces:**

```cpp
struct ProjectManifest {
    std::string name;
    std::string version;
    std::string type;                 // "cli" | "server"
    std::filesystem::path entry;      // canonical, inside root
    std::filesystem::path root;       // directory of yukt.toml
};

std::optional<std::filesystem::path> find_manifest(const std::filesystem::path& start);

std::optional<ProjectManifest> load_manifest(const std::filesystem::path& toml_path,
                                             DiagnosticEngine& diagnostics);
```

**Safety (acceptance, not extra scope):**

1. Top-level keys only: `name`, `version`, `type`, `entry`.
2. Any `[table]` including `[dependencies]` → `Invalid manifest`, detail names the table, suggestion “Dependencies are 0.18.2.”
3. Duplicate keys → `Invalid manifest`.
4. Unknown keys → `Invalid manifest`.
5. `name` matches `^[A-Za-z_][A-Za-z0-9_]*$`.
6. `version` matches `^[0-9]+\.[0-9]+\.[0-9]+$`.
7. `type` is `cli` or `server`.
8. `entry` is relative, ends with `.yukt`, contains no `..` component, is not absolute, is not empty.
9. `canonical(root)` and `canonical(root / entry)` must succeed. The entry canonical path must equal the root canonical path or be a descendant (`entry.native().compare(0, root.native().size(), root.native()) == 0` plus the next character is the preferred separator). Symlinks that escape fail this check.
10. Missing entry file → `Invalid manifest`, “Entry file does not exist.”

TOML subset: `key = "value"` lines, `#` to end of line, optional spaces, no arrays, no floats, no triple quotes, no escaped quotes inside strings for 0.18.1 (if `"` appears inside a value, `Invalid manifest`). UTF-8. `\n` and `\r\n`.

- [ ] **Step 1: Write failing tests**

Helper:

```cpp
std::filesystem::path make_tree(const std::string& toml, const std::string& entry_rel = "src/main.yukt") {
    const auto root = std::filesystem::temp_directory_path() / ("yukt-man-" + std::to_string(getpid()) + "-" + std::to_string(++g_n));
    std::filesystem::create_directories(root / "src");
    std::ofstream(root / "yukt.toml") << toml;
    std::ofstream(root / entry_rel) << "create main:\n    say 1\n";
    return root;
}

bool load_has_title(const std::filesystem::path& toml, const std::string& title) {
    std::ifstream in(toml);
    std::ostringstream buf;
    buf << in.rdbuf();
    yukt::DiagnosticEngine diagnostics(toml.string(), buf.str());
    const auto loaded = yukt::load_manifest(toml, diagnostics);
    if (loaded.has_value()) {
        return false;
    }
    for (const auto& d : diagnostics.diagnostics()) {
        if (d.title == title) {
            return true;
        }
    }
    return false;
}
```

Cases:

1. Valid `name/version/type/entry` → `load_manifest` returns a value, `entry` is inside `root`, `name == "hello"`.
2. `entry = "../escape.yukt"` → `Invalid manifest`.
3. `entry = "/tmp/x.yukt"` → `Invalid manifest`.
4. Create `root/sub` symlink to `/tmp` or parent; `entry = "sub/x.yukt"` if that canonicalizes outside → `Invalid manifest`.
5. `[dependencies]\nfoo = "1"` → `Invalid manifest`.
6. `foo = "bar"` unknown key → `Invalid manifest`.
7. two `name =` lines → `Invalid manifest`.
8. `find_manifest(root / "src")` equals `root / "yukt.toml"`.
9. `find_manifest` on a dir with no ancestor manifest → `nullopt`.

- [ ] **Step 2: Run — expect FAIL** (`manifest.hpp` missing)

```bash
cmake --build build --target yukt_project_tests
```

- [ ] **Step 3: Implement parser**

Line-oriented scan. Track line numbers for `SourceLocation{line, column, toml_path}`.

```cpp
diagnostics.error(loc, "Invalid manifest", detail, suggestion);
```

`find_manifest`: from `canonical(start)` if it exists, else `start`; if start is a file, use parent; walk `p / "yukt.toml"` then `p = p.parent_path()` until filesystem root.

Do not follow `entry` until after rejecting `..` and absolute paths, then `canonical`.

- [ ] **Step 4: Run tests**

```bash
ctest --test-dir build -R yukt_project_tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit if authorized**

```bash
git add compiler/driver/manifest.hpp compiler/driver/manifest.cpp \
        CMakeLists.txt tests/project/project_tests.cpp
git commit -m "$(cat <<'EOF'
Parse yukt.toml and reject entry paths that leave the project.

EOF
)"
```

---

### Task 3: `yukt new`

**Files:**

- Create: `compiler/driver/project.hpp`
- Create: `compiler/driver/project.cpp`
- Create: `share/yukt/templates/cli/yukt.toml`
- Create: `share/yukt/templates/cli/src/main.yukt`
- Create: `share/yukt/templates/cli/tests/main.yukt`
- Create: `share/yukt/templates/cli/tests/main.yukt.out`
- Create: `share/yukt/templates/server/yukt.toml`
- Create: `share/yukt/templates/server/src/main.yukt`
- Create: `share/yukt/templates/server/tests/main.yukt`
- Create: `share/yukt/templates/server/tests/main.yukt.out`
- Modify: `compiler/main.cpp`
- Modify: `CMakeLists.txt` — add `project.cpp` to `yukt_compiler`; `target_compile_definitions(yukt PRIVATE YUKT_TEMPLATES_DIR=...)` (already on tests)
- Modify: `tests/project/project_tests.cpp` — spawn `YUKT_EXECUTABLE`

**Interfaces:**

```cpp
bool create_project(const std::filesystem::path& dest, const std::string& name, const std::string& type,
                    DiagnosticEngine& diagnostics);
```

CLI:

```text
yukt new <name> --type cli|server
```

`--type` required, exactly those two values. `name` is one path component and a YUKT identifier. Reject empty, `/`, `\`, `.`, `..`, and names that are not identifiers.

Dest = `current_path() / name`. If `exists(dest)` → `Cannot create project`.

Copy `templates_root() / type` recursively into `dest.string() + ".stage." + pid`, replace the `name = "..."` line in `yukt.toml` with the requested name, then `rename(stage, dest)`. On failure `remove_all(stage)` and return false. `dest` must not exist on failure.

Stub templates (Task 6 replaces bodies):

`share/yukt/templates/cli/yukt.toml`:

```toml
name = "hello"
version = "0.1.0"
type = "cli"
entry = "src/main.yukt"
```

`src/main.yukt`:

```text
create main:
    say 1
```

`tests/main.yukt` / `.out`: `say 1` / `1\n`. Server stub identical except `type = "server"`.

`print_usage` adds:

```text
  yukt new <name> --type cli|server  Create a project
  yukt build [file.yukt] [-o <exe>]  Build a file or the project entry
  yukt run [file.yukt] [args]        Run a file or the project entry
  yukt test                          Run tests/**/*.yukt
```

Keep existing inspect lines. `build`/`run` still document the file form.

- [ ] **Step 1: Write failing tests**

Using `run_cmd({YUKT_EXECUTABLE, "new", "hello", "--type", "cli"}, tmp)`:

1. Exit 0; `tmp/hello/yukt.toml` exists; `src/main.yukt` exists; `tests/main.yukt` exists.
2. Second `new hello --type cli` in same cwd → nonzero, stderr contains `Cannot create project`.
3. `new ../x --type cli` → `Invalid project name`.
4. `new hello` without `--type` → `Invalid project type`.
5. `new hello --type web` → `Invalid project type`.

`YUKT_EXECUTABLE` is the CMake define (string). `run_cmd` from Task 1.

- [ ] **Step 2: Run — expect FAIL** (`Unknown option` or missing `new`)

```bash
ctest --test-dir build -R yukt_project_tests --output-on-failure
```

- [ ] **Step 3: Implement**

In `main`, before the `build`/`run` block:

```cpp
if (args[0] == "new") {
    std::string name;
    std::string type;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--type") {
            if (i + 1 >= args.size()) {
                // Invalid project type
                return EXIT_FAILURE;
            }
            type = args[++i];
        } else if (!args[i].empty() && args[i][0] == '-') {
            std::cerr << "Unknown option: " << args[i] << '\n';
            return EXIT_FAILURE;
        } else if (name.empty()) {
            name = args[i];
        } else {
            std::cerr << "Unexpected extra argument: " << args[i] << '\n';
            return EXIT_FAILURE;
        }
    }
    yukt::DiagnosticEngine diagnostics("<cli>", "");
    const auto dest = std::filesystem::current_path() / name;
    if (!yukt::create_project(dest, name, type, diagnostics)) {
        diagnostics.emit(std::cerr);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
```

Diagnostics without a source file still use `error(SourceLocation{1,1}, title, detail, suggestion)`.

`templates_root()`: `YUKT_TEMPLATES` env, else `YUKT_TEMPLATES_DIR` compile define, else `install_prefix() / "share" / "yukt" / "templates"`.

Define `YUKT_TEMPLATES_DIR` on the `yukt` target.

- [ ] **Step 4: Run tests**

```bash
ctest --test-dir build -R 'yukt_project_tests|hello_run' --output-on-failure
```

Expected: PASS. File-form `yukt run examples/hello.yukt` still works.

- [ ] **Step 5: Commit if authorized**

```bash
git add compiler/driver/project.hpp compiler/driver/project.cpp compiler/main.cpp \
        compiler/driver/paths.cpp CMakeLists.txt share/yukt/templates tests/project
git commit -m "$(cat <<'EOF'
Add yukt new with cli and server project skeletons.

EOF
)"
```

---

### Task 4: Project `yukt build` and `yukt run`

**Files:**

- Modify: `compiler/main.cpp` — no-file `build`/`run`
- Modify: `compiler/backend/pipeline.cpp` — `create_directories` on `exe_path` parent before `link_executable`
- Modify: `compiler/backend/llvm_backend.cpp` — if `find_clang()` result is not executable (`access(X_OK)`), set `error` to a message that `pipeline.cpp` maps to `Cannot find linker`
- Modify: `tests/project/project_tests.cpp`

**Interfaces:** consume `find_manifest`, `load_manifest`, existing `compile()`. Produce `root/build/<name>`.

Rules:

- `yukt build file.yukt` unchanged (file wins; default `-o` remains the input stem in cwd unless `-o` given).
- `yukt build` with no file: `find_manifest(cwd)`; none → `Cannot find project` (“Run `yukt new` or pass a `.yukt` file.”). Else `load_manifest`; set `input_path = entry`; if `output_path` empty, set it to `(root / "build" / name).string()`.
- `yukt run` with no file: same, then `CompileAction::Run` **or** build to `build/<name>` and execute that path with `extra_args`. Prefer: set output to `build/<name>`, action `Run` still uses a temp exe today. **Change:** for project run, set `request.output_path` to `build/<name>` and action `Run` currently ignores keeping that file. Spec: `yukt run` builds if needed and runs. Implement: if project mode, `Build` to `build/<name>` if missing or entry newer, then `Run` that executable via existing `run_executable`. Simplest correct 0.18.1: always `Build` to `build/<name>` then spawn it with argv. Do not use the temp-dir `Run` path for project mode so `build/<name>` exists after `yukt run`.
- Extra args after `yukt run` remain program argv.
- `yukt run file.yukt` unchanged (temp exe, no `build/` required).

Helper in `project.cpp`:

```cpp
std::optional<ProjectManifest> require_project(DiagnosticEngine& diagnostics);
```

`require_project` uses `find_manifest(current_path())` and `load_manifest`.

`pipeline.cpp` before link:

```cpp
std::filesystem::create_directories(std::filesystem::path(exe_path).parent_path(), ec);
```

Linker: in `link_executable`, if `!path_executable(clang)`:

```cpp
error = "NO_LINKER";
return false;
```

In `pipeline.cpp`:

```cpp
if (error == "NO_LINKER") {
    report(diagnostics, "Cannot find linker",
           "YUKT links user programs with the host C compiler `clang`.",
           "Install Command Line Tools (macOS) or a clang package (Linux).");
    return output;
}
```

Do not mention LLVM in that suggestion.

- [ ] **Step 1: Write failing tests**

In a temp dir:

```text
run_cmd({yukt, "new", "hello", "--type", "cli"}, tmp)
run_cmd({yukt, "build"}, tmp/"hello")  → status 0, exists hello/build/hello, executable
run_cmd({yukt, "run"}, tmp/"hello")    → status 0, out contains "1"
run_cmd({yukt, "build", "src/main.yukt"}, tmp/"hello") → status 0 (file form)
run_cmd({yukt, "build"}, tmp) → Cannot find project
```

- [ ] **Step 2: Run — expect FAIL** (`Missing input file`)

```bash
ctest --test-dir build -R yukt_project_tests --output-on-failure
```

- [ ] **Step 3: Implement no-file branch** in `main` when `args[0]` is `build` or `run` and `input_path` still empty after flag parse. Do not treat inspect flags as project commands.

- [ ] **Step 4: Run tests**

```bash
ctest --test-dir build -R 'yukt_project_tests|yukt_io_tests|hello_run' --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit if authorized**

```bash
git add compiler/main.cpp compiler/backend/pipeline.cpp compiler/backend/llvm_backend.cpp \
        compiler/driver/project.cpp tests/project/project_tests.cpp
git commit -m "$(cat <<'EOF'
Build and run a yukt.toml project into build/.

EOF
)"
```

---

### Task 5: `yukt test`

**Files:**

- Modify: `compiler/driver/project.hpp` / `project.cpp`
- Modify: `compiler/main.cpp`
- Modify: `tests/project/project_tests.cpp`

**Interfaces:**

```cpp
int run_project_tests(const ProjectManifest& project, std::ostream& out, std::ostream& err,
                      DiagnosticEngine& diagnostics);
```

Discover files under `project.root / "tests"` whose extension is `.yukt`, recursively. Sort by `generic_string()`. Skip directories. Do not treat `*.yukt.out` as tests.

For each file `p`:

1. `CompileRequest` with `action = Run`, `input_path = p.string()`.
2. If `!output.ok` or compile diagnostics errors: print `FAIL <rel>: compile` plus diagnostic text to `err`; fail.
3. If `output.exit_code != 0`: print `FAIL <rel>: exit <code>`.
4. If `p.native() + ".out"` exists, read all bytes; if not equal to `output.program_output`, print `FAIL <rel>: stdout mismatch`.
5. Else print `ok <rel>` to `out`.

`rel` is path relative to `project.root`.

Return `0` iff every test passed. Zero files: `diagnostics.error(..., "No tests found", ..., "Add a file under tests/.")` and return `1`.

`yukt test` extra args: none. Unknown flags error.

- [ ] **Step 1: Write failing tests**

After `yukt new hello --type cli` (stub `say 1` / `.out` `1\n`):

1. `yukt test` in `hello/` → 0, stdout contains `ok tests/main.yukt`.
2. Write `tests/bad.yukt` that `say 2` with `tests/bad.yukt.out` containing `1\n` → `yukt test` nonzero, `stdout mismatch`.
3. Empty `tests/` (remove files) → `No tests found`.
4. `create main:\n    give 1` style compile error file → `FAIL` compile.

- [ ] **Step 2: Run — expect FAIL** (`Unknown option: test` or missing subcommand)

- [ ] **Step 3: Implement** `if (args[0] == "test")` using `require_project` + `run_project_tests`.

- [ ] **Step 4: Run**

```bash
ctest --test-dir build -R yukt_project_tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit if authorized**

```bash
git add compiler/driver/project.hpp compiler/driver/project.cpp compiler/main.cpp tests/project
git commit -m "$(cat <<'EOF'
Add yukt test discovery against tests/*.yukt and optional .out files.

EOF
)"
```

---

### Task 6: Real CLI and server templates

**Files:**

- Modify: `share/yukt/templates/cli/**`
- Modify: `share/yukt/templates/server/**`
- Modify: `tests/project/project_tests.cpp`

**CLI template** (0.16 APIs only):

`src/main.yukt`:

```text
use std/io

create main:
    say io.program
    args = io.args
    if count(args) > 0:
        say args[0]
```

`tests/main.yukt`:

```text
create main:
    say "ok"
```

`tests/main.yukt.out` bytes: `ok\n`.

**Server template** (0.17.1 APIs; tests must not hang):

`src/main.yukt` (blocking `yukt run` is specified):

```text
use std/http
use std/map

create handle(req):
    headers = map.new()
    map.set(headers, "content-type", "text/plain")
    give http.Response(200, headers, "ok")

create main:
    http.serve("127.0.0.1:0", handle)
```

`tests/main.yukt` — completing 0.17.1 shape (must return):

```text
use std/http
use std/net
use std/map

create handle(req):
    headers = map.new()
    map.set(headers, "content-type", "text/plain")
    give http.Response(200, headers, "ok")

create run(ln):
    http.serve_on(ln, handle)

create main:
    bound = net.listen("127.0.0.1:0")
    if bound.ok:
        ln = bound.value
        url = "http://" + net.addr(ln) + "/"
        t = start run(ln)
        r = http.get(url)
        if r.ok:
            say r.value.status
            say r.value.body
        otherwise:
            say r.error.code
        give t
```

`tests/main.yukt.out`:

```text
200
ok
```

Do not add `http.post`, TLS, or extra records.

`yukt.toml` for each type: `type = "cli"` / `"server"`, `entry = "src/main.yukt"`, `version = "0.1.0"`, `name = "hello"` (replaced by `create_project`).

- [ ] **Step 1: Tests**

```text
yukt new t --type cli && cd t && yukt test     # exit 0, < 30s
yukt new s --type server && cd s && yukt test  # exit 0, < 30s
yukt build   # both projects, writes build/<name>
```

Add a 30s bound in the C++ test (fail if `run_cmd` does not return). Implement `run_cmd` with a wait; do not hang the suite.

- [ ] **Step 2: Run — expect FAIL** if stubs still `say 1` vs new `.out`

- [ ] **Step 3: Write the template files above.** `create_project` copies them as-is except `name`.

- [ ] **Step 4: Run**

```bash
ctest --test-dir build -R yukt_project_tests --output-on-failure
```

Expected: PASS. Server `yukt test` returns. Do **not** call `yukt run` on the server template in this test (it may block).

- [ ] **Step 5: Commit if authorized**

```bash
git add share/yukt/templates tests/project
git commit -m "$(cat <<'EOF'
Ship cli and server templates that yukt test can run.

EOF
)"
```

---

### Task 7: Tarball, LLVM bundle, install script

**Files:**

- Create: `scripts/package.sh`
- Create: `scripts/install.sh`
- Create: `tests/project/package_smoke.sh`
- Modify: `CMakeLists.txt` — `install()` rules and `INSTALL_RPATH`
- Modify: `CMakeLists.txt` — CTest `yukt_package_smoke` running the smoke script with `YUKT_BUILD_DIR` / `YUKT_SOURCE_DIR`

**CMake install (contributor and package):**

```cmake
include(GNUInstallDirs)
set(_yukt_install_rpath "$ORIGIN/../lib")
if(APPLE)
    set(_yukt_install_rpath "@loader_path/../lib")
endif()
set_target_properties(yukt PROPERTIES INSTALL_RPATH "${_yukt_install_rpath}")

install(TARGETS yukt RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS yukt_runtime ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY ${CMAKE_SOURCE_DIR}/std/ DESTINATION share/yukt/std)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/share/yukt/templates/ DESTINATION share/yukt/templates)
```

Keep existing **build** rpath (`-Wl,-rpath,${LLVM_LIBRARY_DIR}` on Apple) so `./build/yukt` still runs in `ctest`.

**`scripts/package.sh`:**

```bash
#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${YUKT_BUILD_DIR:-$ROOT/build}"
STAGE="${YUKT_STAGE:-$(mktemp -d)}"
DEST="${YUKT_DIST:-$ROOT/dist}"
cmake --install "$BUILD" --prefix "$STAGE"
# Copy LLVM shared libs of $STAGE/bin/yukt (not /usr/lib, /System, libc, libm, libpthread, libdl)
# macOS: otool -L + install_name_tool -change / -id @loader_path/../lib/<file> + -add_rpath
# Linux: ldd + cp + patchelf --set-rpath '$ORIGIN/../lib' if patchelf exists;
#        otherwise rely on INSTALL_RPATH on yukt and copy .so files next to it
mkdir -p "$DEST"
VERSION="$("$STAGE/bin/yukt" --version | awk '{print $2}')"
OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"
TAR="$DEST/yukt-${VERSION}-${OS}-${ARCH}.tar.gz"
tar -C "$STAGE" -czf "$TAR" .
echo "$TAR"
```

LLVM copy loop (must be in the script, not omitted):

- Parse `otool -L "$STAGE/bin/yukt"` / `ldd`.
- For each dep whose path contains `llvm` or `LLVM` or lives in `LLVM_LIBRARY_DIR` / `$(llvm-config --libdir)` / Homebrew `opt/llvm/lib`: copy into `$STAGE/lib`.
- Repeat on newly copied files until the set is closed.
- Never copy `libc.so*`, `libSystem`, `/usr/lib/*` (except Homebrew), `/System/Library/*`.
- Rewrite `yukt` and copied libs to rpath-relative names.

**Smoke (must fail the task if LLVM dylibs are missing):**

```bash
env -i HOME="$HOME" PATH="/usr/bin:/bin:/usr/sbin:/sbin$( [ -d /Library/Developer/CommandLineTools/usr/bin ] && echo :/Library/Developer/CommandLineTools/usr/bin)" \
  "$STAGE/bin/yukt" --version
```

Unset `DYLD_LIBRARY_PATH`, `LD_LIBRARY_PATH`, `DYLD_FALLBACK_LIBRARY_PATH`. If this fails with `Library not loaded: libLLVM` / `libLLVM-*.so: cannot open`, the copy/rpath loop is wrong — fix packaging, do not document `brew install llvm`.

Then smoke `yukt new` + `yukt build` from that prefix (host `clang` **is** allowed on PATH: it is the user-program linker).

**`scripts/install.sh`:**

```bash
PREFIX="${PREFIX:-$HOME/.yukt}"
# args: tarball path or "-" for stdin
# unpack to $PREFIX.stage.$$
# "$PREFIX.stage.$$/bin/yukt" --version
# backup / swap / restore as in strategy
# echo 'export PATH="'"$PREFIX"'/bin:$PATH"'
```

No `llvm` in user-facing strings except maybe “YUKT includes the libraries it needs.” `grep -i llvm scripts/install.sh` must not match `brew install` / `apt install llvm`.

- [ ] **Step 1: Write `tests/project/package_smoke.sh` that fails if `scripts/package.sh` is missing**

```bash
#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
test -x "$ROOT/scripts/package.sh"
TAR="$(YUKT_BUILD_DIR="${YUKT_BUILD_DIR:-$ROOT/build}" "$ROOT/scripts/package.sh")"
STAGE="$(mktemp -d)"
tar -xzf "$TAR" -C "$STAGE"
env -i HOME="$HOME" PATH="/usr/bin:/bin:/usr/sbin:/sbin" "$STAGE/bin/yukt" --version
```

CTest:

```cmake
add_test(NAME yukt_package_smoke COMMAND bash ${CMAKE_SOURCE_DIR}/tests/project/package_smoke.sh)
set_tests_properties(yukt_package_smoke PROPERTIES
    ENVIRONMENT "YUKT_BUILD_DIR=${CMAKE_BINARY_DIR}"
)
```

- [ ] **Step 2: Run — expect FAIL** (script missing)

```bash
ctest --test-dir build -R yukt_package_smoke --output-on-failure
```

- [ ] **Step 3: Implement `install()` + both scripts.** `chmod +x`. Do not add toml/json libraries. `patchelf` is optional; if absent on macOS we use `install_name_tool` only.

- [ ] **Step 4: Run smoke on this Mac, then keep contributor `ctest` green**

```bash
ctest --test-dir build -R 'yukt_package_smoke|yukt_project_tests|hello_run' --output-on-failure
```

Expected: packaged `yukt --version` works with library path env cleared.

- [ ] **Step 5: Commit if authorized**

```bash
git add CMakeLists.txt scripts/package.sh scripts/install.sh tests/project/package_smoke.sh
git commit -m "$(cat <<'EOF'
Package a prefix tarball that runs yukt without a system LLVM install.

EOF
)"
```

---

### Task 8: Both-host gate and docs

**Files:**

- Modify: `compiler/main.cpp` — `kVersion = "0.18.1"`
- Modify: `CMakeLists.txt` — `project(yukt VERSION 0.18.1 ...)`
- Modify: `docs/YUKT_018_DESIGN.md` — status: 0.18.1 implemented, 0.18.2 specified not implemented
- Modify: `docs/ROADMAP.md`, `AGENTS.md` — 0.18.1 landed; next authorized objective is 0.18.2 spec-to-plan, not coding in this task
- Modify: `docs/LANGUAGE_SPEC.md` — a short **Tooling (0.18.1)** note: `yukt new` / `yukt.toml` / `yukt test`; no new syntax
- Do not claim 0.18.2. Mutex, fmt, LSP, REPL still not implemented.

**Gates:**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Must include `yukt_project_tests`, `yukt_package_smoke`, and every 0.16/0.17 suite.

Linux: GitHub Actions job `linux` in `.github/workflows/ci.yml` (Ubuntu x86-64). If red, fix 0.18.1 only. Do not flip docs on macOS-only green.

**Acceptance from the spec, using packaged `bin/yukt` plus host `clang` on PATH:**

```text
yukt --version
yukt new hello --type cli
cd hello && yukt build && yukt test && yukt run
yukt new api --type server
cd api && yukt build && yukt test
cd hello && yukt build src/main.yukt
```

`yukt run` for the server template is **not** required to return in CI. `yukt test` must.

- [ ] **Step 1: Full macOS `ctest`.** Expected: PASS.

- [ ] **Step 2: Linux CI `ctest` including package smoke.** If `patchelf` is required, install it in the Linux job only (`sudo apt-get install -y patchelf`) as a **packaging-host** tool, not an application-developer instruction.

- [ ] **Step 3: Docs + version bump.** `grep -n llvm scripts/install.sh` must not advise installing LLVM.

- [ ] **Step 4: Confirm freeze files untouched** (`git diff -- benchmarks/results-0.11.json docs/BENCHMARKS.md benchmarks/results-0.15.macos-arm64.json` empty).

- [ ] **Step 5: Commit if authorized**

```bash
git add compiler/main.cpp CMakeLists.txt docs/YUKT_018_DESIGN.md docs/ROADMAP.md \
        AGENTS.md docs/LANGUAGE_SPEC.md
git commit -m "$(cat <<'EOF'
Mark 0.18.1 project workflow implemented after both-host tests.

EOF
)"
```

---

## Out of this plan

0.18.2 path/git/lockfile, 0.18.3 index, `yukt publish`, formatter, LSP, REPL, mutex, Windows, CUDA, new syntax, backend redesign, Homebrew formula as the ship-gate (may wrap the tarball later).

---

## Spec coverage

| Spec § | Task |
| --- | --- |
| §3 one binary, file form kept | 3–4 |
| §4 install tarball, no LLVM for app developers | 1, 7, 8 |
| §5 `yukt new`, layout, TOML subset | 2, 3 |
| §6 build/run/test, `build/`, `.out` | 4, 5 |
| §7 templates | 6 |
| §8–9 deps/index | **not this plan** |
| §11 frozen benches / frontend / native meaning | Global constraints |
| §12 acceptance on install-only machine | 7, 8 |
| Self-contained LLVM dylibs, runtime `.a`, stdlib, exe paths | 1, 7 |
| Manifest `entry` escape, malformed, missing files | 2 |
| Both hosts | 8 |

## Placeholder scan

No TBD. `[dependencies]` is an error in Task 2, not 0.18.2 work. `YUKT_PACKAGED` was not introduced; install rpath is enough. `patchelf` is a packaging-host tool on Linux, not an app-developer dependency.
