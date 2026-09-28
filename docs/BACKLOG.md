# Backlog

Work that was looked at and deliberately not done, each entry with the reason it was not done.
Nothing here is a correctness regression; the list exists so that every one of these is a recorded
decision rather than an oversight.

## Accepted as it is

- **`AssertFailedException` mapped to `InternalException`** in `src/exec/exec_common.cpp`. This is
  the only remaining path that can invalidate the whole DuckDB instance, and it is debug-only:
  CMake's default `CMAKE_CXX_FLAGS_RELEASE` is `-O3 -DNDEBUG`, this project never touches `NDEBUG`,
  and `NDEBUG` compiles upstream's `pgassert`/`pgassertwm` to `((void)0)`, so the arm cannot fire
  in a release build. Where it can fire it is the right mapping — an upstream invariant violation
  genuinely should stop everything in a debug build — so it is accepted rather than changed.
- **DuckDB CLI's `.mode json` does not quote string elements inside a `LIST` column.**
  `SELECT [1,2,3]` renders as `[1, 2, 3]`, which is valid JSON, but a `VARCHAR[]` column renders as
  bare identifiers, for example `[col0, col1, directed]`, which `json.loads` rejects — so
  `duckdbcli.query()` raises when a query returns such a column. `check_signatures.py` works around
  it locally by joining each list with `array_to_string(..., chr(31))` in SQL and splitting the
  result in Python. The fix does not belong in the shared `duckdbcli` module: the fault is in the
  CLI's own JSON renderer, not in anything `duckdbcli` decides.
- **`duckdbcli.query()` starts two subprocesses per query** — one for `DESCRIBE`, one for the
  query itself — and each one reruns the whole SQL preamble, which now loads five CSV fixtures.
  Across the 134 blocks that reach `db.query()` out of the 142 total across the fifteen selected
  pages (`pgr_dijkstra`, `pgr_dijkstraCost`, `pgr_dijkstraCostMatrix`, `pgr_dijkstraNear`,
  `pgr_dijkstraNearCost`, `pgr_withPoints`, `pgr_withPointsCost`, `pgr_withPointsCostMatrix`,
  `pgr_bdDijkstra`, `pgr_bdDijkstraCost`, `pgr_bdDijkstraCostMatrix`, `pgr_bellmanFord`,
  `pgr_edwardMoore`, `pgr_dagShortestPath`, `pgr_binaryBreadthFirstSearch`; 8 blocks are skipped),
  that is 268 process starts; each call now has a 120-second timeout. Acceptable for a tool that
  only runs at regeneration time, not in any inner loop a developer waits on repeatedly. Measured
  with all fifteen MVP functions implemented: 9.53 s for the whole argument-free run.
- **`pgr_dijkstraNearCost` runs pgRouting's driver in path mode (`only_cost = false`) and keeps each
  path's closing row (`edge = -1`)**, because the pinned release's only_cost `Path` constructor in
  `include/cpp_common/path.hpp` leaves `m_tot_cost` uninitialized, which `post_process` then sorts
  and truncates by. Fixed upstream on `develop` by commit `b27576bd58`. Cost: `pgr_dijkstraNearCost`
  materializes full paths instead of just their costs. Revert (switch the NearCost overloads back
  to `only_cost = true` and `Projection::COST`) once the pinned release initializes
  `m_tot_cost`; see the `Projection::COST_OF_PATH` comment in `src/functions/function_spec.hpp`.
- **A defaulted parameter given both positionally and by name silently prefers the positional
  value.** For example `pgr_dijkstraNearCost(edges_sql, 6, [10, 11, 1], true, 2, cap := 1)` returns
  the same rows as `cap := 2` (the positional value), not `cap := 1` (the named one); PostgreSQL
  rejects such a call outright. DuckDB resolves named and positional arguments independently and
  this extension does not add its own check for the overlap.
- **`pgr_bdDijkstra*`, `pgr_bdAstar*` and `pgr_binaryBreadthFirstSearch` cannot be cancelled once
  the algorithm is running.** Upstream's `include/bdDijkstra/bdDijkstra.hpp`,
  `include/bdAstar/bdAstar.hpp`, `include/cpp_common/bidirectional.hpp` and
  `include/breadthFirstSearch/binaryBreadthFirstSearch.hpp` never call `CHECK_FOR_INTERRUPTS`,
  unlike `pgr_bellmanFord`, `pgr_edwardMoore` and `pgr_dagShortestPath`, whose headers do poll it
  inside their main loop. PostgreSQL runs the same unmodified algorithm bodies and is equally
  uncancellable there, so this is not a regression introduced by the shared interrupt path.
- **`pgr_ksp` with a negative `K` returns no rows instead of an error.** Upstream's C entry
  (`src/ksp/ksp.c`) returns before calling its driver when `K < 0`, under a "TODO return error
  message" comment, while `pgr_withPointsKSP` raises `Invalid value of 'K'` for the same input.
  This extension reproduces both answers (`RequestCheck::KSP_K` and `WITH_POINTS_KSP` in
  `src/include/pgrouting/driver_kind.hpp`); follow upstream if it turns the first into an error.
- **`pgr_aStar` / `pgr_aStarCost` / `pgr_aStarCostMatrix` can return a dearer-than-optimal cost when
  a source has several targets.** `distance_heuristic::operator()` in
  `include/astar/astar.hpp` minimizes over the still-open goals but erases a goal from that set as
  soon as it is first discovered, before its cost is final; from then on the heuristic is no longer
  admissible for that goal, so A* can settle for a worse route to it. It never returns a cheaper
  route than Dijkstra's, only a costlier one, and only when a source has more than one target (see
  `test/sql/astar_workshop.test`, which checks the one-target-per-source case exactly and the
  many-target case only for that direction). PostgreSQL runs the same unmodified header and has the
  same behaviour, so this is not a regression introduced here; it is not fixed here because upstream
  files are never modified. A candidate to report upstream.
- **Driver allocations are not counted by `memory_limit`.** pgRouting's drivers build their graphs
  and results with `new` and `malloc`, which DuckDB's buffer manager never sees, so a query's
  routing work can exceed `memory_limit` (and, in Wasm, press against the 4 GB address space)
  without DuckDB refusing it. PostgreSQL's `work_mem` does not bound them either. Accepted: the
  alternative is an allocator shim inside unmodified upstream code.

## Closed as declined

- **A pgrouting-specific log tag.** `DUCKDB_LOG_INFO` has no tag parameter, and adding one needs a
  `LogType` class plus the `DUCKDB_LOG` macro. The extension's messages are therefore not
  filterable by tag name in `duckdb_logs`. They are still visible: they arrive with an empty
  `type` and the level upstream chose — `INFO` for a notice, `DEBUG` for a log line — which is
  what `test/sql/dijkstra_errors.test` asserts against. Reopen only if something needs tag-based
  filtering for its own sake.

## Test tooling gaps

- **Three of the eight tie-downgraded `pgr_dijkstra` blocks have no exact-row counterpart anywhere
  in the repository.** `q93`, `q133` and `q136` are pinned only by the `differing_rows` count
  recorded for them in `test/pgrouting_ties.json`; `test/sql/dijkstra.test` keeps an exact-row block
  for every other tie-downgraded shape (q4, q5, q6, q7 and q96) but never mentions q93, q133 or
  q136. This is acceptable today: every tie-downgraded block, these three included, still asserts
  upstream's own `start_vid`/`end_vid`, row count and total `agg_cost` (the companion query's
  `GROUP BY start_vid, end_vid` / `count(*)` / `max(agg_cost)`); only the specific `node`/`edge`
  route on the equal-cost tie is left unpinned. All three are, in fact, the same documented (12, 7)
  tie that q96's exact-row block in `test/sql/dijkstra.test` already covers (q93 and q133 each
  compute that single pair directly; q136's 12-row result contains it as one of four combinations,
  and its `differing_rows: 2` matches q93/q133's own two differing cells exactly). A future
  contributor who wants an exact-row regression signal for one of these three specifically, rather
  than relying on q96's coverage of the same tie, should add a hand-written block for it to
  `test/sql/dijkstra.test` the way q4/q5/q6/q7/q96 already have one. Those per-family functions' own
  tie-downgraded blocks (18 in `test/pgrouting_ties.json` across pgr_bdDijkstra, pgr_bellmanFord,
  pgr_edwardMoore, pgr_dagShortestPath and pgr_binaryBreadthFirstSearch) are pinned the same way: by
  `start_vid`/`end_vid`, row count and `agg_cost` only, never the specific node/edge route on the
  tie. The hand-written layer-3 tests for those families (`test/sql/bd_dijkstra.test`,
  `test/sql/bellman_ford.test`, `test/sql/dag_shortest_path.test` and `test/sql/binary_bfs.test`)
  likewise assert `start_vid`, `end_vid` and `agg_cost`, never a path.

## Unreachable today

Each of these would be a change no test could observe, so none of them is made:

- `duckdb_pgrouting::ColumnClass::CHAR1` in `src/include/pgrouting/input_access.hpp` is never
  produced by `ClassOf`, so the `CHAR1` arm of `Accepts` in `src/pg_compat/src/get_check_data.cpp`
  cannot fire.
- `getText` in `src/pg_compat/src/get_check_data.cpp` returns a `std::malloc`'d buffer with no
  owner. Unreachable on the pgr_dijkstra path, which fetches no TEXT column.
- `seq[i] = NumericCast<int32_t>(k + 1)` in `src/exec/result_emitters.cpp` throws past 2^31
  result rows. Upstream uses `int` for `seq` too, so matching it is the deliberate choice.
- `TagFunctions` in `src/functions/spec_functions.cpp` resolves the same catalog entry
  once per spec, so two specs disagreeing on `pgrouting_name` for one public name would resolve
  silently to the first.
- The "No elements found" text in `get_pgarray` (`src/pg_compat/src/get_check_data.cpp`) has no
  test, because no registered pgr_dijkstra overload reaches it: an empty id list, for example
  `pgr_dijkstra(sql, []::BIGINT[], 3)`, returns zero rows rather than raising.
- **A list-typed result column would never compare equal between upstream and this build.** Upstream
  renders one as `{4,7}` in its psql transcript, while a DuckDB list stringifies as `[4, 7]`. No
  `pgr_dijkstra` documentation query returns a list-typed column today, so nothing exercises this,
  but the first category that does will meet it and the comparison in `gen_docqueries_tests.py` will
  need to normalize both sides first.
- **`to_list_literal`'s empty-but-present array case is unexercised.** `export_sampledata.py`'s
  `to_list_literal` turns PostgreSQL's `{}` into `[]`; an `""` cell (no array text at all) instead
  short-circuits to `""` before that conversion runs. No row in the current fixture set carries an
  empty-but-present array, so the `{}` → `[]` path is not pinned by any test.

## No coverage, by nature

- The `InternalException` and `OutOfMemoryException` arms of the error mapping in
  `src/exec/exec_common.cpp` have no test coverage, because neither is reachable from plain SQL
  without fault injection. The rest of that mapping is pinned by `test/sql/dijkstra_errors.test`,
  but any claim that the mapping is covered has to carry this qualifier — it is never true
  unqualified.
- **`dijkstra/dijkstraCostMatrix.pg` produces no generated test file.** Its only runnable block,
  q1, passes a scalar subquery (`(SELECT array_agg(id) FROM vertices WHERE id IN (...))`) as the
  vertex array, which DuckDB rejects in the argument of a table function that is not in-out
  (`Table function cannot contain subqueries`), so it is skipped in `test/pgrouting_skip.json`; q2
  calls `pgr_TSP`, which this extension does not implement. Coverage for `pgr_dijkstraCostMatrix`
  instead comes from the hand-written `test/sql/dijkstra_cost.test`. The workaround for the
  scalar-subquery shape, used there and available to any caller: `SET VARIABLE ids = (SELECT
  list(id) FROM vertices WHERE ...);` then pass `getvariable('ids')` as the vertex-array
  argument. Every other stem of the category has a generated file. `pgr_TSP` surfaces solely
  through `check_signatures.py`'s unimplemented-function list.
- **`bdDijkstra/bdDijkstraCostMatrix.pg` produces no generated test file either**, for the same
  reason: its only runnable block, q2, passes a scalar subquery as the vertex array and is skipped
  in `test/pgrouting_skip.json`; q3 calls `pgr_TSP`. Coverage for `pgr_bdDijkstraCostMatrix` comes
  from the hand-written `test/sql/bd_dijkstra.test`, which also pins upstream's q2 rows.
- **`astar/aStarCostMatrix.pg` and `bdAstar/bdAstarCostMatrix.pg` produce no generated test file
  either**, for the same reason: q2 passes a scalar subquery and is skipped in
  `test/pgrouting_skip.json`, and q3 calls `pgr_TSP`. `test/sql/astar.test` and
  `test/sql/bd_astar.test` pin both q2 results.
- **`driving_distance/dijksraDD-issue729.pg` and `spanningTree/randomSpanTree.pg` produce no
  generated test file.** The first is a regression script with no named blocks; the second
  documents `pgr_randomSpanTree`, which pgRouting 4.0 does not publish (it is absent from
  `sql/sigs/pgrouting--4.0.sig`).
- **`ksp/turnRestrictedPath.pg` produces no generated test file** until `pgr_turnRestrictedPath`
  is ported: its page is selected by stem, and no implemented function has that name.
- **A depth- or distance-limited `pgr_kruskalBFS`/`kruskalDFS`/`kruskalDD`/`primBFS`/`primDFS`/
  `primDD` generated block asserts only the tree structure, not which vertices are reached.**
  Its spanning-forest invariant (each root's single depth-0 row, no repeated node, every other
  row hanging off its predecessor one level up) holds for any equal-cost forest a `max_depth` or
  a `distance` bound is applied to, but not which vertices fall inside that bound: which forest is
  built among equal-cost edges depends on the C++ standard library, and a bounded walk of a
  different forest can legitimately stop at different vertices. Only the unlimited `*BFS`/`*DFS`
  forms (no `max_depth` given) add a fifth column asserting each root's total row count, since an
  unlimited walk always reaches its whole connected component regardless of which forest was
  built. `test/sql/spanning_tree.test` is the hand-written coverage for a bound's own behaviour
  (`max_depth`/`distance` limits, negative-value errors, defaults).

## Open decision

- **`ListOfRows` and `ORDER BY`: performance against run-to-run reproducibility.** `ListOfRows` in
  `src/functions/spec_functions.cpp` emits `(SELECT list(_pgr_row) FROM (<sql>)
  _pgr_row)`. DuckDB's `list()` does not preserve scan order once the scan runs on several threads,
  and Boost breaks an equal-cost predecessor tie by adjacency-list insertion order, so on a large
  enough edge set the same query on the same data can return different — equally optimal — routes
  between runs. Adding `ORDER BY _pgr_row` would buy reproducibility at the price of a sort over
  the whole edge set on every query, and DuckDB struct ordering does not cover every child type, so
  it would need verification of its own. It may belong behind a setting rather than as a default.
  Not decided. The `pgr_withPoints` derived inputs (edges of points, edges without points) share the
  property: they are built by a join and an anti-join whose output order DuckDB does not
  guarantee either.
