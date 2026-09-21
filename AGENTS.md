# AGENTS.md

Guidance for coding agents (and humans) working on this repository.

## Overview

`duckdb-routing` is a DuckDB extension (`LOAD routing`) that exposes pgRouting's graph algorithms
with pgRouting's SQL API minus the `pgr_` prefix (`pgr_dijkstra` → `dijkstra`). pgRouting's C++
code (`third_party/pgrouting`, a submodule pinned to a release tag) is compiled **unmodified** and
statically linked; only its PostgreSQL-specific layers are replaced. License: GPL-2.0-or-later.

Current state: the extension registers `dijkstra` — pgRouting's five `pgr_dijkstra` signatures,
each in a short form and a positional-`directed` form, ten DuckDB table functions in total — and
`DuckDB_pgRouting_Version()`. Every `dijkstra` call is rewritten into a call to pgRouting's own
unified `do_shortestPath` driver.

## Layout

- `duckdb/` — DuckDB submodule (tracks `main`).
- `extension-ci-tools/` — DuckDB's extension build/CI tooling (submodule).
- `third_party/pgrouting/` — pgRouting submodule, pinned to a release tag.
- `cmake/pgrouting_sources.cmake` — explicit list of compiled upstream files; also reads the
  pgRouting version from upstream's `CMakeLists.txt`.
- `src/routing_extension.cpp` — extension entry point (`LoadInternal`).
- `src/pg_compat/` — PostgreSQL stub headers and the replaced upstream definitions. Compiled
  only into pg_compat and pgRouting translation units: the stub `postgres.h` defines `ERROR` as
  a macro and DuckDB has an enumerator of that name, so the two must never meet in one
  translation unit. `src/include/routing/input_access.hpp` is the seam between the two worlds
  and includes neither.
- `src/exec/` — input registry, driver invocation and the internal in-out table function.
- `src/functions/` — the declarative overload table and the public function registration.
- `test/sql/` — sqllogictests. `test/sql/pgrouting/<category>/<name>.test` is generated from
  upstream's documentation queries and is never hand-edited.
- `test/data/pgrouting_sample/` — CSV fixtures rebuilt from upstream's committed sample data.
- `test/pgrouting_skip.json`, `test/pgrouting_ties.json`, `test/pgrouting_not_ported.json` — the
  three control files the test tooling reads; see *Test tooling* for who owns each.
- `scripts/` — the Python test tooling and its `unittest` suite under `scripts/tests/`.
- `scripts/git-hooks/` — optional local commit guards (`make install-hooks`).

## Build and test

Prerequisites: CMake, Ninja, a C++17 compiler, Python 3.9 or newer, vcpkg (for Boost), and for
Wasm: Emscripten
3.1.71 and Node 24 (the version CI pins for the Wasm test workflow; Node 22.22.0 is also known to
run all three Wasm unittest variants locally).

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

## Test tooling

`scripts/` holds three Python tools over two shared modules, plus their `unittest` suite. They read
upstream's committed fixtures and drive the built `duckdb` binary; none of them needs PostgreSQL,
and none of them ever writes under `third_party/pgrouting`.

- `scripts/pgparse.py` — upstream's two fixture formats only: the `/* -- <name> */` blocks of a
  `.pg` file, and the psql aligned-output tables, notices and errors of a `.result` file.
- `scripts/duckdbcli.py` — runs SQL through `build/release/duckdb` and returns typed rows. That
  binary already has the extension statically linked, so nothing is `LOAD`ed and no Python DuckDB
  package is involved; a pip-installed driver would exercise a different build than the one CI
  ships.
- `scripts/export_sampledata.py` — rebuilds `test/data/pgrouting_sample/*.csv` from upstream's
  `tools/testers/sampledata.pg` and `docqueries/src/sampledata.result`. `--check` fails when the
  committed fixtures no longer match what those upstream files say.
- `scripts/gen_docqueries_tests.py` — translates upstream's documentation queries into
  `test/sql/pgrouting/<category>/<name>.test`, executing every translated query so it can tell an
  equal-cost tie from a defect. `--check` regenerates and fails on any difference.
- `scripts/check_signatures.py` — compares upstream's `sql/sigs/pgrouting--<ver>.sig` against
  `duckdb_functions()` through the `pgrouting_name` tag, never by raw row count: one upstream
  signature is intentionally registered as two DuckDB variants.

The generators execute queries against the release binary, so build it first:

```bash
GEN=ninja make release && python3 scripts/gen_docqueries_tests.py --category dijkstra
GEN=ninja make release && python3 scripts/export_sampledata.py
python3 -m unittest discover -s scripts/tests
```

Three JSON control files live under `test/`:

| file | owner | content |
|---|---|---|
| `test/pgrouting_skip.json` | human | documentation blocks skipped entirely, each with a reason |
| `test/pgrouting_ties.json` | the generator | blocks downgraded to tie-insensitive assertions, each with the observed difference — never hand-edited; regenerate instead |
| `test/pgrouting_not_ported.json` | human | upstream functions deliberately not ported, each with a reason |

CI has three workflows. `MainDistributionPipeline.yml` builds every DuckDB platform through
DuckDB's reusable extension workflow. `WasmTests.yml` builds DuckDB's `unittest` for all three Wasm
variants and runs the sqllogictests under Node. `Checks.yml` builds release on linux_amd64 and runs
the two generators in `--check` mode, the tooling's unit tests, the signature comparison and
`test/sql/collisions.test`; a `--check` failure means the committed artifact is stale, an
equal-cost tie has flipped, or something regressed, and all three want a human.

## Architecture rules

- Never modify files under `third_party/pgrouting`. PostgreSQL-dependent upstream code is replaced
  only by include-order shadowing (a compatibility include directory placed before pgRouting's
  `include/`) or by link-time substitution (the replaced `.cpp` is not compiled; the extension
  defines the same symbols).
- Add upstream files to `cmake/pgrouting_sources.cmake` one by one; no globbing.
- Emscripten builds must keep C++ exception catching enabled: pgRouting's algorithms throw and
  catch internally.
- Upstream is written for GCC and Clang, and MSVC compiles it here for the first time. Where an
  upstream header uses a compiler extension MSVC lacks, supply the equivalent spelling as a
  compile definition from `CMakeLists.txt` rather than editing the header: `__PRETTY_FUNCTION__`
  is mapped to `__FUNCSIG__` that way, matching what upstream itself does for the same identifier
  in the headers where it did guard it.
- `src/pg_compat/include` must precede `third_party/pgrouting/include` on the include path; that
  ordering is what replaces upstream's PostgreSQL-dependent headers.
- PostgreSQL allows a parameter with a default to be passed positionally or by name; DuckDB never
  matches a named parameter positionally. Every upstream signature is therefore registered twice,
  with and without a trailing positional `directed`.
- The order in which an input query's rows reach pgRouting is not guaranteed stable: DuckDB's
  `list()` does not preserve scan order once the scan runs on several threads, and `ORDER BY` in
  the caller's own SQL does not change that. Boost breaks an equal-cost predecessor tie by
  adjacency-list insertion order and pgRouting inserts edges in fetch order, so on a large enough
  edge set the same query on the same data may return different equal-cost routes between runs.
  Every answer is still cost-optimal, and PostgreSQL's unordered scans give pgRouting the same
  property.

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
   the CSV test fixtures under `test/data/` (CSV has no comment syntax either, and a `#` line
   would be read as data), the Markdown documentation (`README.md`, `AGENTS.md`, `CLAUDE.md`) and
   `LICENSE`, and `.gitignore` / `.gitmodules` (git metadata, not source). In a sqllogictest the
   header goes *after* the `# name:` / `# description:` / `# group:` block, which is parsed
   positionally.
7. The upstream↔public function name mapping (`pgr_dijkstra` ↔ `dijkstra`, and so on) lives only
   in the `pgrouting_name` function tag, read back from `duckdb_functions()`; never duplicate it
   in a script.
8. The Python tooling under `scripts/` uses the standard library only. No `pip install` step exists
   in CI or in the developer prerequisites, and its tests use `unittest`, not `pytest`. A
   dependency that would need one is a reason to change the approach, not to add the dependency.

`make install-hooks` installs commit-msg/pre-commit hooks that reject messages, paths or added
lines matching the extended regular expressions listed in the untracked file
`.git/info/forbidden-patterns`.
