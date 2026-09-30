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

Eighty-five pgRouting functions — two hundred and thirty-two of pgRouting 4.0's signatures — plus
`pgr_version()`.
The [pgRouting documentation](https://docs.pgrouting.org/4.0/en/) describes each algorithm, its
parameters and its result columns, all of which this extension keeps.

| Family | Functions |
|---|---|
| Dijkstra | `pgr_dijkstra`, `pgr_dijkstraCost`, `pgr_dijkstraCostMatrix`, `pgr_dijkstraNear`, `pgr_dijkstraNearCost` |
| With points | `pgr_withPoints`, `pgr_withPointsCost`, `pgr_withPointsCostMatrix` |
| Bidirectional Dijkstra | `pgr_bdDijkstra`, `pgr_bdDijkstraCost`, `pgr_bdDijkstraCostMatrix` |
| A* | `pgr_aStar`, `pgr_aStarCost`, `pgr_aStarCostMatrix`, `pgr_bdAstar`, `pgr_bdAstarCost`, `pgr_bdAstarCostMatrix` |
| Other shortest paths | `pgr_bellmanFord`, `pgr_edwardMoore`, `pgr_dagShortestPath`, `pgr_binaryBreadthFirstSearch` |
| K shortest paths | `pgr_ksp`, `pgr_withPointsKSP` |
| Via | `pgr_dijkstraVia`, `pgr_withPointsVia` |
| Turn restrictions | `pgr_trsp`, `pgr_trsp_withPoints`, `pgr_trspVia`, `pgr_trspVia_withPoints`, `pgr_turnRestrictedPath` |
| Traveling salesperson | `pgr_TSP`, `pgr_TSPeuclidean` |
| Components | `pgr_connectedComponents`, `pgr_strongComponents`, `pgr_biconnectedComponents`, `pgr_articulationPoints`, `pgr_bridges`, `pgr_makeConnected` |
| Driving distance | `pgr_drivingDistance`, `pgr_withPointsDD` |
| Spanning trees | `pgr_kruskal`, `pgr_kruskalBFS`, `pgr_kruskalDFS`, `pgr_kruskalDD`, `pgr_prim`, `pgr_primBFS`, `pgr_primDFS`, `pgr_primDD` |
| Traversal | `pgr_breadthFirstSearch`, `pgr_depthFirstSearch` |
| Coloring | `pgr_sequentialVertexColoring`, `pgr_bipartite`, `pgr_edgeColoring` |
| Ordering | `pgr_cuthillMckeeOrdering`, `pgr_kingOrdering`, `pgr_sloanOrdering`, `pgr_topologicalSort` |
| All pairs | `pgr_johnson`, `pgr_floydWarshall` |
| Metrics | `pgr_betweennessCentrality`, `pgr_bandwidth`, `pgr_degree` |
| Graph structure | `pgr_isPlanar`, `pgr_lineGraph`, `pgr_lineGraphFull`, `pgr_transitiveClosure`, `pgr_lengauerTarjanDominatorTree`, `pgr_hawickCircuits`, `pgr_stoerWagner` |
| Flow | `pgr_maxFlow`, `pgr_pushRelabel`, `pgr_boykovKolmogorov`, `pgr_edmondsKarp`, `pgr_maxFlowMinCost`, `pgr_maxFlowMinCost_Cost`, `pgr_edgeDisjointPaths`, `pgr_maxCardinalityMatch`, `pgr_chinesePostman`, `pgr_chinesePostmanCost` |
| Contraction | `pgr_contraction`, `pgr_contractionDeadEnd`, `pgr_contractionLinear`, `pgr_contractionHierarchies` |
| Utilities | `pgr_extractVertices`, `pgr_findCloseEdges` |

Functions deliberately not ported are listed, each with its reason, in
`test/pgrouting_not_ported.json`.

## Differences from pgRouting

- **Inner queries are DuckDB SQL**, and array arguments are DuckDB lists (`[7, 10]` or
  `ARRAY[7, 10]`).
- **Named arguments** use DuckDB's `:=` (or `=>`). A `NULL` argument returns no rows, as it does
  for pgRouting's `STRICT` functions.
- **One-value functions work both ways.** `pgr_bandwidth`, `pgr_isPlanar`, `pgr_maxFlow`,
  `pgr_maxFlowMinCost_Cost` and `pgr_chinesePostmanCost` return a single value, as in pgRouting:
  `SELECT pgr_isPlanar('…')` and `SELECT * FROM pgr_isPlanar('…')` both work (a table function of
  one row and a scalar macro of the same name). Like every function here, none takes a column as
  an argument: neither the query nor, for `pgr_maxFlow` and `pgr_maxFlowMinCost_Cost`, the
  vertices; every argument must be a constant.
- **Equal-cost ties.** The order in which the inner query's rows reach the algorithm is not fixed
  (DuckDB scans in parallel), so where two routes cost exactly the same, the same query may return
  either one between runs. Every answer is still optimal; PostgreSQL's unordered scans give
  pgRouting the same property. `pgr_kruskal*` may also return a different, equally minimal
  spanning forest on macOS or Wasm than on Linux, since the C++ standard library's tie-break among
  equal-cost edges differs (libstdc++ vs. libc++); `pgr_prim*` uses Boost's own heap instead, but
  its ties are asserted the same way for uniformity. With `heap_paths := true`, `pgr_ksp`,
  `pgr_withPointsKSP` and `pgr_turnRestrictedPath` also return the candidate paths left on Yen's
  heap, and which candidates those are — and their costs — depend on the input order too.
- **TSP tours depend on the input order.** `pgr_TSP` and `pgr_TSPeuclidean` approximate the
  shortest round trip, and which tour they find — and its total cost — depends on the order the
  inner query's rows reach the algorithm, which DuckDB does not fix (an `ORDER BY` in the inner
  query does not either). `pgr_TSPeuclidean` without `start_id` or `end_id` also starts from
  whichever row arrives first (with `end_id` alone, the tour starts at `end_id`). Every answer is
  still a valid tour; see `docs/BACKLOG.md` for upstream's other TSP behaviours this extension
  keeps.
- **Graph orderings, colorings and joins depend on the input order too.** `pgr_makeConnected`,
  `pgr_sequentialVertexColoring`, `pgr_bipartite`, `pgr_edgeColoring`, `pgr_topologicalSort` and the
  three bandwidth orderings (`pgr_cuthillMckeeOrdering`, `pgr_kingOrdering`, `pgr_sloanOrdering`)
  return one of several valid answers, chosen by the order the edges reach the algorithm; the
  orderings' ties also follow the C++ standard library, so they can differ between Linux, macOS,
  Windows and Wasm. So do the orientation and order of `pgr_lineGraph`'s rows, the negative ids
  `pgr_lineGraphFull` hands out, the order inside `pgr_transitiveClosure`'s lists, where each of
  `pgr_hawickCircuits`' circuits starts, which of several equally cheap cuts `pgr_stoerWagner`
  reports, and `pgr_lengauerTarjanDominatorTree`'s `seq` and `idom` (which, as upstream writes it,
  is the `seq` of the dominator's row, not a vertex id). So do which edges carry
  `pgr_pushRelabel`'s, `pgr_boykovKolmogorov`'s, `pgr_edmondsKarp`'s and `pgr_maxFlowMinCost`'s
  flow (never its value or cost), which paths `pgr_edgeDisjointPaths` returns (never how many),
  which edges `pgr_maxCardinalityMatch` picks (never how many; the choice also follows the C++
  standard library, so it can differ between platforms), and where `pgr_chinesePostman`'s tour
  starts: at the first edge row's source. So do the contraction functions' shortcut ids and, in an
  undirected graph, their direction, which vertex a contracted tree collapses onto, and all of
  `pgr_contractionHierarchies`' ranking (`metric`, `vertex_order`) and shortcuts. Every answer is
  valid for its problem; see `docs/BACKLOG.md` for the upstream behaviours these functions keep.
- **`pgr_degree` returns its rows sorted by vertex**; upstream's order is unspecified.
- **`pgr_edgeDisjointPaths` with many sources and one target swaps `start_vid` and `end_vid`**,
  as pgRouting's own SQL wrapper does: `start_vid` shows the target, `end_vid` the source.
- **`pgr_contractionHierarchies` is slow on large graphs and its output is not a query-ready
  hierarchy.** Thousands of road segments take seconds to minutes;
  `vertex_order` is not the contraction order, and upstream's shortcuts do not always complete the
  hierarchy. See `docs/BACKLOG.md`.
- **Memory.** pgRouting's own allocations are not counted against DuckDB's `memory_limit`.
- **A turn-restricted route re-routed over more than about ten thousand edges, or a Chinese
  Postman tour of more than about five thousand steps,** can overflow a worker thread's stack and
  end the process on macOS (upstream's recursion); see `docs/BACKLOG.md`.
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
published Release and runs them on pgRouting's sample graph, on the Hiroshima road network of
the [pgRouting workshop](https://workshop.pgrouting.org/) with the workshop's queries adapted to
DuckDB, or on Overture Maps roads for an area you choose on the map, which its queries read from
Overture's servers and turn into a routable graph step by step, with the routes drawn on a map.

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
