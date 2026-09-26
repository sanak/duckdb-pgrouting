# duckdb-pgrouting

A DuckDB extension that brings [pgRouting](https://pgrouting.org/)'s graph algorithms to DuckDB,
with pgRouting's own SQL API, `pgr_` function names included. pgRouting's C++ code is compiled
unmodified and statically linked; only its PostgreSQL-specific layers are replaced.

> **Unofficial.** This extension is not affiliated with, endorsed by or sponsored by the pgRouting
> project or the Open Source Geospatial Foundation. pgRouting is a trademark of its owners; DuckDB
> is a trademark of the DuckDB Foundation.

## Install

Each [GitHub Release](https://github.com/sanak/duckdb-pgrouting/releases) is built for one DuckDB
version, named in its title and in its asset names. An extension loads only into the exact DuckDB
version it was built for, so pick the newest Release built for yours.

The binaries are not signed by DuckDB, so DuckDB has to be started with unsigned extensions
allowed. Do that only if you trust this project.

```sql
-- Start the CLI with: duckdb -unsigned
-- (other clients: set allow_unsigned_extensions = true in the connection configuration)
SELECT version();   -- your DuckDB version, e.g. v1.5.5
PRAGMA platform;    -- your platform, e.g. osx_arm64

INSTALL 'https://github.com/sanak/duckdb-pgrouting/releases/download/<tag>/pgrouting.<duckdb version>.<platform>.duckdb_extension.gz';
LOAD pgrouting;
```

Every Release's notes list the ready-to-paste `INSTALL` line for each platform. To update, run
`FORCE INSTALL` with the newer Release's URL. Keep the tag in the URL rather than using
`releases/latest/download/`: the latest Release may be built for a newer DuckDB version than yours.

Platforms: `linux_amd64`, `linux_arm64`, `osx_amd64`, `osx_arm64`, `windows_amd64`,
`windows_amd64_mingw`, and DuckDB-Wasm (`wasm_mvp`, `wasm_eh`, `wasm_threads`; see below).

Releases are built from `main`, which targets the current DuckDB v1.5 release. The
`v2.0-cyanoptera` branch targets DuckDB 2.0, which is not released yet.

## Example

```sql
CREATE TABLE edges (id BIGINT, source BIGINT, target BIGINT, cost DOUBLE, reverse_cost DOUBLE);
INSERT INTO edges VALUES (1, 5, 6, 1, 1), (2, 6, 10, -1, 1), (3, 6, 7, 1, 1), (4, 10, 7, 1, -1);

SELECT * FROM pgr_dijkstra('SELECT id, source, target, cost, reverse_cost FROM edges', 5, 7);
```

```
┌───────┬──────────┬───────────┬─────────┬───────┬───────┬────────┬──────────┐
│  seq  │ path_seq │ start_vid │ end_vid │ node  │ edge  │  cost  │ agg_cost │
│ int32 │  int32   │   int64   │  int64  │ int64 │ int64 │ double │  double  │
├───────┼──────────┼───────────┼─────────┼───────┼───────┼────────┼──────────┤
│     1 │        1 │         5 │       7 │     5 │     1 │    1.0 │      0.0 │
│     2 │        2 │         5 │       7 │     6 │     3 │    1.0 │      1.0 │
│     3 │        3 │         5 │       7 │     7 │    -1 │    0.0 │      2.0 │
└───────┴──────────┴───────────┴─────────┴───────┴───────┴────────┴──────────┘
```

The inner query is ordinary DuckDB SQL, so it can read any table, view or file DuckDB can.
Parameters with a default can be passed positionally, as in pgRouting, or by name:

```sql
SELECT * FROM pgr_dijkstraCost(
  'SELECT id, source, target, cost, reverse_cost FROM edges', 5, [7, 10], directed := false);
```

Every function carries a one-line description and an example in DuckDB's catalog:

```sql
SELECT DISTINCT function_name, description FROM duckdb_functions()
WHERE function_name LIKE 'pgr\_%' ESCAPE '\' ORDER BY ALL;
```

## Functions

Eighteen pgRouting functions — seventy-six of pgRouting 4.0's signatures — plus `pgr_version()`.
The [pgRouting documentation](https://docs.pgrouting.org/4.0/en/) describes each algorithm, its
parameters and its result columns, all of which this extension keeps.

| Family | Functions |
|---|---|
| Dijkstra | `pgr_dijkstra`, `pgr_dijkstraCost`, `pgr_dijkstraCostMatrix`, `pgr_dijkstraNear`, `pgr_dijkstraNearCost` |
| With points | `pgr_withPoints`, `pgr_withPointsCost`, `pgr_withPointsCostMatrix` |
| Bidirectional Dijkstra | `pgr_bdDijkstra`, `pgr_bdDijkstraCost`, `pgr_bdDijkstraCostMatrix` |
| Other shortest paths | `pgr_bellmanFord`, `pgr_edwardMoore`, `pgr_dagShortestPath`, `pgr_binaryBreadthFirstSearch` |
| Components | `pgr_connectedComponents` |
| Utilities | `pgr_extractVertices`, `pgr_findCloseEdges` |

Functions deliberately not ported are listed, each with its reason, in
`test/pgrouting_not_ported.json`.

## Differences from pgRouting

- **Inner queries are DuckDB SQL**, and array arguments are DuckDB lists (`[7, 10]` or
  `ARRAY[7, 10]`).
- **Named arguments** use DuckDB's `:=` (or `=>`). A `NULL` argument returns no rows, as it does
  for pgRouting's `STRICT` functions.
- **Equal-cost ties.** The order in which the inner query's rows reach the algorithm is not fixed
  (DuckDB scans in parallel), so where two routes cost exactly the same, the same query may return
  either one between runs. Every answer is still optimal; PostgreSQL's unordered scans give
  pgRouting the same property.
- **Memory.** pgRouting's own allocations are not counted against DuckDB's `memory_limit`.
- **Geometry needs the spatial extension.** `pgr_findCloseEdges`, and `pgr_extractVertices` on
  geometry, call [duckdb-spatial](https://duckdb.org/docs/stable/core_extensions/spatial/overview):
  run `INSTALL spatial; LOAD spatial;` first. DuckDB does not autoload spatial; with
  `autoload_known_extensions` on, the function loads spatial itself, and with
  `autoinstall_known_extensions` also on (the default in release builds) it first installs it from
  the extension repository. `GEOMETRY` arguments and columns are DuckDB's own type, so
  `'POINT(1 2)'::GEOMETRY` works without it.
- **`pgr_findCloseEdges`'s point-array signature** takes `GEOMETRY[]`. `ST_MakePoint` returns
  `POINT_2D`, and a list of those does not cast to `GEOMETRY[]` implicitly; build the list with
  `ST_Point` instead, or cast each element to `GEOMETRY`. A single `ST_MakePoint` point works.
- **No subquery as a table-function argument.** DuckDB does not accept one where pgRouting's
  documentation passes `(SELECT geom FROM ...)` as a point; use `SET VARIABLE` and
  `getvariable()` instead.
- **`dryrun := true`** writes the generated query to DuckDB's log (`duckdb_logs`, level INFO)
  instead of a NOTICE. Logging is off by default, so run `SET enable_logging = true;` first, with a
  readable `logging_storage` such as `'memory'`, and read the query from `duckdb_logs`.
- **`pgr_findCloseEdges`'s `side`** follows the edge's direction at the closest point, because
  duckdb-spatial has no single-sided buffer. Upstream's corner cases are kept (on the line, end
  points included: `r`; past either end off the line, or tolerance 0: `l`), but near a vertex where
  an edge turns the two rules can differ. Rows come in input-point order, then by distance.
- **`pgr_findCloseEdges`** gives each input point its own `cap`, even when the same point is given
  twice; upstream groups identical points into one `cap`.
- **`pgr_extractVertices`** always sorts `in_edges` and `out_edges`, and returns its rows ordered
  by `id`.

## DuckDB-Wasm

To try it without installing anything, open the
[Playground](https://sanak.github.io/duckdb-pgrouting/): it serves the Wasm builds of every
published Release and runs them on pgRouting's sample graph, with the routes drawn on a map.

GitHub serves Release downloads without CORS headers, so DuckDB-Wasm cannot fetch the Wasm assets
from a Release directly. Host `pgrouting.<duckdb version>.<variant>.duckdb_extension.wasm` on your
own origin under the name `pgrouting.duckdb_extension.wasm` (DuckDB takes the extension's name from
the file name) and load it with `allowUnsignedExtensions: true`:

```sql
LOAD 'https://example.org/wasm/v1.5.5/wasm_eh/pgrouting.duckdb_extension.wasm';
```

Memory is the limit in the browser: wasm32 caps the heap at 4 GB, and browsers usually allow
1–2 GB, which is on the order of one to two million edges. Beyond that a query fails with an
out-of-memory error rather than crashing the page. `wasm_threads` needs a page served with
cross-origin isolation (COOP/COEP headers); `wasm_eh` is the usual choice.

## Building from source

Clone with submodules (`git clone --recurse-submodules …`, or
`git submodule update --init --recursive` in an existing clone), then see `AGENTS.md` for the
prerequisites and the build and test commands. `docs/RELEASE.md` describes how releases are made.

## License

GPL-2.0-or-later (see `LICENSE`), because pgRouting's GPL-2.0-or-later code is statically linked.
The corresponding source of each released binary is this repository at the commit its Release
names, including the submodules recorded there.

The workshop-hiroshima data under `test/data/workshop-hiroshima/` is © OpenStreetMap contributors,
available under the Open Database License 1.0; see its `NOTICE.md`.
