# AGENTS.md

Guidance for coding agents (and humans) working on this repository.

## Overview

`duckdb-routing` is a DuckDB extension (`LOAD routing`) that exposes pgRouting's graph algorithms
with pgRouting's SQL API minus the `pgr_` prefix (`pgr_dijkstra` → `dijkstra`). pgRouting's C++
code (`third_party/pgrouting`, a submodule pinned to a release tag) is compiled **unmodified** and
statically linked; only its PostgreSQL-specific layers are replaced. License: GPL-2.0-or-later.

Current state: the extension loads and compiles the PostgreSQL-free part of pgRouting, but
registers no SQL functions yet.

## Layout

- `duckdb/` — DuckDB submodule (tracks `main`).
- `extension-ci-tools/` — DuckDB's extension build/CI tooling (submodule).
- `third_party/pgrouting/` — pgRouting submodule, pinned to a release tag.
- `cmake/pgrouting_sources.cmake` — explicit list of compiled upstream files; also reads the
  pgRouting version from upstream's `CMakeLists.txt`.
- `src/routing_extension.cpp` — extension entry point (`LoadInternal`).
- `test/sql/` — sqllogictests.
- `scripts/git-hooks/` — optional local commit guards (`make install-hooks`).

## Build and test

Prerequisites: CMake, Ninja, a C++17 compiler, vcpkg (for Boost), and for Wasm: Emscripten
3.1.71 and Node 24.

```bash
git submodule update --init --recursive
export VCPKG_TOOLCHAIN_PATH=<vcpkg>/scripts/buildsystems/vcpkg.cmake
GEN=ninja make debug           # or: make release / make relassert
make test_debug                # all sqllogictests
build/debug/test/unittest test/sql/routing.test   # a single test file
```

Wasm (requires `source <emsdk>/emsdk_env.sh`, and `VCPKG_TOOLCHAIN_PATH` exported as above):

```bash
GEN=ninja VCPKG_TARGET_TRIPLET=wasm32-emscripten make wasm_eh   # loadable extension
GEN=ninja make wasm_unittest W1_VARIANT=wasm_eh   # unittest compiled to Wasm (also wasm_mvp, wasm_threads)
make test_wasm_unittest W1_VARIANT=wasm_eh        # run test/* under Node
```

Do not pass `DUCKDB_PLATFORM=` to the Wasm targets: the inherited
`extension-ci-tools/makefiles/duckdb_extension.Makefile` appends its own
`-DDUCKDB_EXPLICIT_PLATFORM=<variant>` later on the same cmake command line, which wins.

sqllogictest `query` directives accept only the column-type characters `T` (text), `I` (integer)
and `R` (floating point). There is no `B` for boolean, so a boolean column is declared `T` and
asserted against `true` / `false`.

## Architecture rules

- Never modify files under `third_party/pgrouting`. PostgreSQL-dependent upstream code is replaced
  only by include-order shadowing (a compatibility include directory placed before pgRouting's
  `include/`) or by link-time substitution (the replaced `.cpp` is not compiled; the extension
  defines the same symbols).
- Add upstream files to `cmake/pgrouting_sources.cmake` one by one; no globbing.
- Emscripten builds must keep C++ exception catching enabled: pgRouting's algorithms throw and
  catch internally.

## Conventions

1. English only for everything committed or published: code, comments, Markdown, commit messages,
   Issues, PRs.
2. Committed content must be self-contained: never reference local, untracked planning documents
   (their paths or section numbers). State the rationale inline.
3. Upstream pgRouting files are never modified.
4. Commit messages: one-line conventional commits, imperative mood, under 72 characters, no
   trailing period.
5. The default branch is `main`.
6. Every new source file carries an SPDX header (`# SPDX-License-Identifier: GPL-2.0-or-later`
   for the `#`-comment class). Deliberate exemptions: `vcpkg.json` (JSON has no comment syntax),
   the Markdown documentation (`README.md`, `AGENTS.md`, `CLAUDE.md`) and `LICENSE`, and
   `.gitignore` / `.gitmodules` (git metadata, not source). In a sqllogictest the header goes
   *after* the `# name:` / `# description:` / `# group:` block, which is parsed positionally.

`make install-hooks` installs commit-msg/pre-commit hooks that reject messages, paths or added
lines matching the extended regular expressions listed in the untracked file
`.git/info/forbidden-patterns`.
