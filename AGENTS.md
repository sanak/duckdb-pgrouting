# AGENTS.md

Guidance for coding agents (and humans) working on this repository.

## Overview

`duckdb-pgrouting` is a DuckDB extension (`LOAD pgrouting`) that exposes pgRouting's graph
algorithms with pgRouting's own SQL API, function names included (`pgr_dijkstra`,
`pgr_bdDijkstra`). pgRouting's C++ code (`third_party/pgrouting`, a submodule pinned to a release
tag) is compiled **unmodified** and statically linked; only its PostgreSQL-specific layers are
replaced. License: GPL-2.0-or-later.

Current state: the extension registers `pgr_dijkstra`, `pgr_dijkstraCost`, `pgr_dijkstraCostMatrix`,
`pgr_dijkstraNear`, `pgr_dijkstraNearCost`, `pgr_withPoints`, `pgr_withPointsCost`,
`pgr_withPointsCostMatrix`, `pgr_bdDijkstra`, `pgr_bdDijkstraCost`, `pgr_bdDijkstraCostMatrix`,
`pgr_bellmanFord`, `pgr_edwardMoore`, `pgr_dagShortestPath`, `pgr_binaryBreadthFirstSearch`,
`pgr_connectedComponents`, `pgr_extractVertices` and `pgr_findCloseEdges` — pgRouting's
seventy-six corresponding signatures, each registered once per number of its defaulted parameters
passed positionally — and `pgr_version()`. The `pgr_dijkstra` and `pgr_withPoints` families call
pgRouting's unified `do_shortestPath` driver; with points given, DuckDB also materializes the two
edge queries that driver derives from the edge and points SQL. The other five families call their
own per-family `pgr_do_*` drivers through one adapter on the pg_compat side.
`pgr_connectedComponents` goes through the same adapter and exec function, which returns its
vertex/component pairs instead of path rows. `pgr_extractVertices` and `pgr_findCloseEdges`, which
upstream writes in PL/pgSQL, are reimplemented as bind_replace functions. Each binds the caller's
edge query, picks one of upstream's modes, and rewrites the call into a fixed DuckDB query
(`src/functions/sql_template.cpp`). Their geometry work calls duckdb-spatial's `ST_*` functions at
run time.
Every public function carries a catalog description and an example (`duckdb_functions().description`
/ `.examples`), registered from `src/functions/function_docs.cpp`.

## Layout

- `duckdb/` — DuckDB submodule: the latest v1.5 release tag on `main`, a `v2.0-cyanoptera`
  commit on the `v2.0-cyanoptera` next branch.
- `extension-ci-tools/` — DuckDB's extension build/CI tooling (submodule).
- `third_party/pgrouting/` — pgRouting submodule, pinned to a release tag.
- `cmake/pgrouting_sources.cmake` — explicit list of compiled upstream files; also reads the
  pgRouting version from upstream's `CMakeLists.txt`.
- `cmake/extension_version.cmake` — the version a tagged build reports, passed to DuckDB from
  `extension_config.cmake` because DuckDB v1.5's own derivation never matches a tag.
- `src/pgrouting_extension.cpp` — extension entry point (`LoadInternal`).
- `src/pg_compat/` — PostgreSQL stub headers and the replaced upstream definitions. Compiled
  only into pg_compat and pgRouting translation units: the stub `postgres.h` defines `ERROR` as
  a macro and DuckDB has an enumerator of that name, so the two must never meet in one
  translation unit. `src/include/pgrouting/input_access.hpp` is the seam between the two worlds
  and includes neither. `src/pg_compat/src/old_style_drivers.cpp` is the one translation unit
  that calls pgRouting's per-family drivers, whose headers include `postgres.h`; the DuckDB side
  reaches it through `src/include/pgrouting/old_style_drivers.hpp`, which includes neither world.
- `src/exec/` — input registry, driver invocation, the internal in-out table function, and the copy
  of upstream's withPoints derived-query key template (`withpoints_keys.cpp`).
- `src/functions/` — the declarative overload table (`shortest_path_specs.cpp`), the catalog
  descriptions and examples (`function_docs.cpp`), the public function registration, the two
  PL/pgSQL reimplementations (`extract_vertices.cpp`, `find_close_edges.cpp`) and their shared
  template machinery (`sql_template.cpp`).
- `test/sql/` — sqllogictests. `test/sql/pgrouting/<category>/<name>.test` is generated from
  upstream's documentation queries and is never hand-edited.
- `test/data/sampledata/` — CSV fixtures rebuilt from upstream's committed sample data.
- `test/configs/skip_spatial.json` — unittest config that skips every test tagged `spatial`.
- `test/data/workshop-hiroshima/` — the pgRouting workshop's OpenStreetMap road network as
  Parquet (ODbL; see its `NOTICE.md`), rebuilt by `scripts/export_workshop_hiroshima.sh`
  (one-off, Docker).
- `test/pgrouting_skip.json`, `test/pgrouting_ties.json`, `test/pgrouting_not_ported.json` — the
  three control files the test tooling reads; see *Test tooling* for who owns each.
- `scripts/` — the Python test and release tooling and its `unittest` suite under
  `scripts/tests/`.
- `scripts/git-hooks/` — optional local commit guards (`make install-hooks`).
- `test/w2/` — W2, the browser smoke test of the Wasm build: a Node project of its own (pinned
  `@duckdb/duckdb-wasm`, Playwright, esbuild) with a standard-library static server.
- `site/` — the Playground published on GitHub Pages: an MIT-licensed Vanilla TypeScript + Vite
  project (Node 24, Biome, `node --test`) that runs the extension in DuckDB-Wasm on the sample
  graph and draws results with MapLibre. `npm run collect` copies the sample CSVs and every
  published Release's Wasm builds into `site/public/` before a dev server or build.
- `docs/RELEASE.md` — how a release is cut; `docs/UPSTREAM_SYNC.md` — how pgRouting is bumped.

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
build/debug/test/unittest test/sql/pgrouting.test   # a single test file
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

Tests that need duckdb-spatial start with `tags spatial` and run `INSTALL spatial;` and
`LOAD spatial;` as statements (network): spatial is not autoloadable, and `require spatial` only
finds statically linked extensions. `--test-config test/configs/skip_spatial.json` skips them; the
Wasm unittest always passes it, and on `v2.0-cyanoptera` the Makefile exports it as
`DUCKDB_TEST_CONFIG`, because no spatial binary exists for an unreleased DuckDB.

## Release lines and releases

Until DuckDB 2.0.0 ships there are two lines. `main` (stable) is built against the current
DuckDB v1.5 release and is what users install; the `v2.0-cyanoptera` branch (next) is built
against DuckDB's branch of that name. Every change lands on `main` first and is cherry-picked
onto the branch; the branch differs only in its submodule pins, its v2.0 source adaptations, the
two `if: false` lines of `MainDistributionPipeline.yml`, the one extra test
`test/sql/dijkstra_interrupt.test` and the Makefile line that exports `DUCKDB_TEST_CONFIG`. No
`#if` compatibility macros go on either line. When DuckDB
2.0.0 ships, `v2.0-cyanoptera` is merged into `main` and a v1.5 maintenance branch is cut first.

Releases are tags on `main` only. A pushed `v*` tag runs the distribution pipeline, and
its `Draft GitHub Release` job turns that run's nine artifacts into a draft Release:
`scripts/release_assets.py` names them
`pgrouting.<duckdb version>.<platform>.duckdb_extension.gz` / `…<variant>.duckdb_extension.wasm`,
refuses a build whose reported version is not the tag, and writes `SHA256SUMS` and the notes. A
person publishes the draft. W2 (`test/w2/`, run through `W2.yml`) checks the Wasm build in a real
browser before and after tagging. The whole procedure is `docs/RELEASE.md`.

W2 locally (downloads Playwright's Chromium once):

```bash
cd test/w2 && npm ci && npx playwright install chromium
gh run download <run id> -p 'pgrouting-*-extension-wasm_*' -D /tmp/w2-download
node prepare-extension.mjs /tmp/w2-download /tmp/w2-ext
W2_MODE=artifact W2_EXTENSION_DIR=/tmp/w2-ext npm test
```

## Test tooling

`scripts/` holds three Python test tools and one release tool over two shared modules, plus
their `unittest` suite. The test tools read upstream's committed fixtures and drive the built
`duckdb` binary; none of them needs PostgreSQL, and none of them ever writes under
`third_party/pgrouting`.

- `scripts/pgparse.py` — upstream's two fixture formats only: the `/* -- <name> */` blocks of a
  `.pg` file, and the psql aligned-output tables, notices and errors of a `.result` file.
- `scripts/duckdbcli.py` — runs SQL through `build/release/duckdb` and returns typed rows. That
  binary already has the extension statically linked, so nothing is `LOAD`ed and no Python DuckDB
  package is involved; a pip-installed driver would exercise a different build than the one CI
  ships.
- `scripts/export_sampledata.py` — rebuilds `test/data/sampledata/*.csv` from upstream's
  `tools/testers/sampledata.pg` and `docqueries/src/sampledata.result`. `--check` fails when the
  committed fixtures no longer match what those upstream files say.
- `scripts/gen_docqueries_tests.py` — turns upstream's documentation queries into
  `test/sql/pgrouting/<category>/<name>.test`, executing every query so it can tell an
  equal-cost tie from a defect. `--check` regenerates and fails on any difference. Pages of a
  function tagged `pgrouting_requires = spatial` are generated with `tags spatial` and run with
  spatial loaded, unless `PGROUTING_NO_SPATIAL=1` (set by `Checks.yml` on the next line), which
  keeps them unchecked. Upstream's WKT cells are respelled as duckdb-spatial writes them
  (`POINT (1 2)`).
- `scripts/check_signatures.py` — compares upstream's `sql/sigs/pgrouting--<ver>.sig` against
  `duckdb_functions()` through the `pgrouting_name` tag, never by raw row count: one upstream
  signature is intentionally registered as several DuckDB variants, one per number of its
  defaulted parameters passed positionally.
- `scripts/release_assets.py` — turns one distribution run's artifacts into GitHub Release assets
  (see *Release lines and releases*); run by the release job, not by hand.
- `scripts/export_workshop_hiroshima.sh` — rebuilds `test/data/workshop-hiroshima/*.parquet` with
  Docker and osm2pgrouting. It is a data tool, not test tooling: the no-PostgreSQL rule does not
  cover it, and no test or CI job runs it.

The generators execute queries against the release binary, so build it first:

```bash
GEN=ninja make release && python3 scripts/gen_docqueries_tests.py
GEN=ninja make release && python3 scripts/export_sampledata.py
python3 -m unittest discover -s scripts/tests
```

Three JSON control files live under `test/`:

| file | owner | content |
|---|---|---|
| `test/pgrouting_skip.json` | human | documentation blocks skipped entirely, each with a reason |
| `test/pgrouting_ties.json` | the generator | blocks downgraded to tie-insensitive assertions, each with the observed difference — never hand-edited; regenerate instead |
| `test/pgrouting_not_ported.json` | human | upstream functions deliberately not ported, each with a reason |

CI has five workflows. `MainDistributionPipeline.yml` builds every DuckDB platform through
DuckDB's reusable extension workflow, and for a pushed `v*` tag drafts the GitHub Release.
`W2.yml` runs only by hand (`workflow_dispatch`), at release time. `WasmTests.yml` builds DuckDB's
`unittest` for all three Wasm variants and runs the sqllogictests under Node. `Checks.yml` builds
release on linux_amd64 and runs the two generators in `--check` mode, the tooling's unit tests,
the signature comparison and `test/sql/collisions.test`; a `--check` failure means the committed
artifact is stale, an equal-cost tie has flipped, or something regressed, and all three want a
human. `Pages.yml` checks, tests and builds the Playground (`site/`) on pull requests, and
deploys it to GitHub Pages from `main` — on pushes and by hand after a Release is published.

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
  matches a named parameter positionally. An upstream signature with k defaulted parameters is
  therefore registered k + 1 times, passing the first 0..k of them positionally, and every
  variant accepts all k by name. The defaulted parameters are data (`OptionalParam` in
  `src/functions/function_spec.hpp`), in upstream's declaration order.
- The order in which an input query's rows reach pgRouting is not guaranteed stable: DuckDB's
  `list()` does not preserve scan order once the scan runs on several threads, and `ORDER BY` in
  the caller's own SQL does not change that. Boost breaks an equal-cost predecessor tie by
  adjacency-list insertion order and pgRouting inserts edges in fetch order, so on a large enough
  edge set the same query on the same data may return different equal-cost routes between runs.
  Every answer is still cost-optimal, and PostgreSQL's unordered scans give pgRouting the same
  property.
- Every public function has a row in `src/functions/function_docs.cpp`: a one-line description
  and one example, written in this project's own words (pgRouting's documentation is CC-BY-SA
  and is never copied). Registration throws when a row is missing, `test/sql/descriptions.test`
  asserts every `pgrouting_name`-tagged function has both, and
  `scripts/tests/test_catalog_examples.py` runs every example on the sample graph.
- Geometry comes from duckdb-spatial at run time only: never built, linked or vendored. spatial is
  not autoloadable, so a function that needs it calls `RequireSpatial`
  (`src/functions/sql_template.cpp`), which names `INSTALL spatial; LOAD spatial` when it is
  missing; with `autoload_known_extensions` on it loads spatial itself, and with
  `autoinstall_known_extensions` also on (the release-build default) it installs it first. Geometry
  data for the Playground is stored as WKB in a BLOB column, not GeoParquet, which DuckDB-Wasm
  cannot read yet.
- `pgr_extractVertices` and `pgr_findCloseEdges` are translations of upstream's PL/pgSQL
  (`sql/utilities/*.sql`), not calls into it; `docs/UPSTREAM_SYNC.md` has them re-diffed at every
  bump.

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
   for the `#`-comment class, `// SPDX-…` in JavaScript, `<!-- SPDX-… -->` in HTML). Deliberate
   exemptions: `vcpkg.json` and `test/w2/package.json` / `package-lock.json` (JSON has no comment
   syntax; `package.json` states the licence in its `license` field), the CSV test fixtures under
   `test/data/` (CSV has no comment syntax either, and a `#` line would be read as data), the
   Parquet data files under `test/data/` (binary), `test/data/workshop-hiroshima/NOTICE.md`
   (Markdown) and `test/configs/skip_spatial.json` (JSON), the Markdown documentation
   (`README.md`, `AGENTS.md`, `CLAUDE.md`) and `LICENSE`, and `.gitignore` / `.gitmodules` (git
   metadata, not source).
   Under `site/` the identifier is `MIT` instead (`site/LICENSE`), in the same comment forms plus
   `/* … */` in CSS; `site/package.json`, `site/package-lock.json`, `site/tsconfig.json`,
   `site/.nvmrc` and `site/NOTICE.md` are exempt like their counterparts above. In a
   sqllogictest the header goes *after* the `# name:` / `# description:` / `# group:` block,
   which is parsed positionally.
7. The `pgrouting_name` function tag marks every function that corresponds to an upstream
   pgRouting function and holds that function's upstream name, which is also its public name.
   The tooling reads the set of implemented functions back from `duckdb_functions()` through
   that tag and nowhere else; never keep a second list of them in a script.
   `pgrouting_requires = spatial` marks a function whose queries need duckdb-spatial; the tooling
   reads it the same way.
8. The Python tooling under `scripts/` uses the standard library only. No `pip install` step exists
   in CI or in the developer prerequisites, and its tests use `unittest`, not `pytest`. A
   dependency that would need one is a reason to change the approach, not to add the dependency.
   `test/w2/` is outside this rule: it is a browser test and needs a pinned DuckDB-Wasm, Playwright
   and a bundler. Its own server and helpers still use Node's standard library only, and every
   package it has is pinned to an exact version.
   `site/` is outside it too: the Playground is a Vite + TypeScript project whose packages are
   pinned to exact versions, and its unit tests run with `node --test`.

`make install-hooks` installs commit-msg/pre-commit hooks that reject messages, paths or added
lines matching the extended regular expressions listed in the untracked file
`.git/info/forbidden-patterns`.
