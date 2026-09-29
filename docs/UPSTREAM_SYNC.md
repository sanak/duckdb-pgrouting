# Upstream sync procedure

pgRouting is a submodule pinned to a release tag and is never modified. Moving to a new tag is
the following checklist, in order. Every step either passes or tells you exactly what changed.

1. Bump `third_party/pgrouting` to the new tag and build (`GEN=ninja make release`). Compile
   errors are expected only in `src/exec` (a driver signature changed), in `src/pg_compat` (a new
   PostgreSQL seam appeared, or a per-family driver's signature changed in one of the
   `src/pg_compat/src/drivers_*.cpp` files).
   An error anywhere else means an upstream file joined the compiled set without being added to
   `cmake/pgrouting_sources.cmake`.

   If upstream moved one of the per-family drivers (pgr_aStar, pgr_bdAstar, pgr_bdDijkstra,
   pgr_bellmanFord, pgr_edwardMoore, pgr_dagShortestPath, pgr_binaryBreadthFirstSearch,
   pgr_drivingDistance, pgr_withPointsDD, pgr_kruskal, pgr_prim, pgr_breadthFirstSearch,
   pgr_depthFirstSearch, pgr_ksp, pgr_withPointsKSP, pgr_dijkstraVia, pgr_withPointsVia,
   pgr_trsp, pgr_trsp_withPoints, pgr_trspVia, pgr_trspVia_withPoints, pgr_turnRestrictedPath,
   pgr_TSP, pgr_TSPeuclidean)
   onto `do_shortestPath`, delete its case in its
   `src/pg_compat/src/drivers_*.cpp` file, switch that family's rows in its
   `src/functions/*_specs.cpp` table to `DriverKind::SHORTEST_PATH` with the flags its new SQL
   wrapper passes, and drop its driver file from `cmake/pgrouting_sources.cmake`.

   The functions on upstream's unified `do_ordering`, `do_allpairs` and `do_metrics`
   (pgr_cuthillMckeeOrdering, pgr_kingOrdering, pgr_sloanOrdering, pgr_topologicalSort, pgr_johnson,
   pgr_floydWarshall, pgr_bandwidth) are called from `src/pg_compat/src/drivers_unified.cpp` with
   the `Which` value their C entries pass; a changed signature of any of the three
   drivers shows up there.

   The graph-analysis functions (pgr_strongComponents, pgr_biconnectedComponents,
   pgr_articulationPoints, pgr_bridges, pgr_makeConnected, pgr_sequentialVertexColoring,
   pgr_bipartite, pgr_edgeColoring, pgr_betweennessCentrality) call their own per-family drivers
   from `src/pg_compat/src/drivers_graph.cpp`; if upstream moves one onto a unified driver (for
   example `do_coloring` or `do_metrics`), call it from `src/pg_compat/src/drivers_unified.cpp`
   with the `Which` value its C entry passes, and drop its per-family driver file from
   `cmake/pgrouting_sources.cmake`.

   The structural functions (pgr_isPlanar, pgr_lineGraph, pgr_lineGraphFull, pgr_transitiveClosure,
   pgr_lengauerTarjanDominatorTree, pgr_hawickCircuits, pgr_stoerWagner) call their per-family
   drivers from `src/pg_compat/src/drivers_analysis.cpp`; treat them the same way.

   The flow functions (pgr_maxFlow, pgr_pushRelabel, pgr_boykovKolmogorov, pgr_edmondsKarp,
   pgr_maxFlowMinCost, pgr_maxFlowMinCost_Cost, pgr_edgeDisjointPaths, pgr_maxCardinalityMatch,
   pgr_chinesePostman, pgr_chinesePostmanCost) call their per-family drivers from
   `src/pg_compat/src/drivers_flow.cpp`; treat them the same way. Re-read
   `include/chinese/chinesePostman.hpp` at every bump: the adapter's `PostmanStartsOffTheGraph`
   guard exists because its constructor crashes when the first edge row's source lies on no
   positive-cost direction and the positive directions are one connected piece or none, and goes
   once upstream no longer does.
2. Update `cmake/pgrouting_sources.cmake` for added or removed upstream files, one entry at a
   time. Never glob: the list is deliberately explicit so that a new upstream file is a decision
   rather than an accident. Do not list a `*_process.cpp` (those talk to PostgreSQL and this
   extension replaces them) and do not list an upstream file that upstream's own CMake omits.
3. Re-export the fixtures: `python3 scripts/export_sampledata.py`. A diff here means upstream
   changed the sample graph, which changes every expected result below it.
4. Regenerate the documentation-query tests: `python3 scripts/gen_docqueries_tests.py`. This runs
   every translated query against the build you just made, so it reports three different things
   and they must not be confused:
   - a clean run with no diff: nothing to do;
   - a diff in `test/pgrouting_ties.json`: an equal-cost tie now falls the other way, or a
     `pgr_kruskal*`/`pgr_prim*` block's spanning-forest invariant entry changed. That is a Boost,
     vcpkg or row-order change, not a routing defect. Review it and commit it.
   - a non-zero exit with a `Mismatch`: this build's answer is not an equal-cost alternative to
     upstream's. Investigate before committing anything.
   Read the diff against upstream's release notes either way.
   Only each implemented function's own documentation page is processed; a generated file that
   is no longer produced (its function's page vanished upstream, or it emits nothing) is removed
   by a regenerating run and reported by `--check`.
5. Re-diff the three PL/pgSQL functions this extension reimplements:
   `git -C third_party/pgrouting diff <old tag> <new tag> -- sql/utilities/extractVertices.sql sql/utilities/findCloseEdges.sql sql/metrics/degree.sql`.
   Carry any change to their column checks, modes, error texts or query into
   `src/functions/extract_vertices.cpp`, `src/functions/find_close_edges.cpp`
   and `src/functions/degree.cpp`. The generated `test/sql/pgrouting/utilities/*.test` and
   `metrics/degree.test` cover only the documented queries, so a silent change elsewhere would go
   unnoticed. Also re-diff `src/common/check_parameters.c`, transcribed into the A* request check in
   `src/exec/exec_function.cpp`, and the A* SQL wrappers' constant flags (`normal := false` in the
   many-to-one forms, astarCost's `ORDER BY`, bdAstar's `NUMERIC` defaults): their wording and flags
   are pinned only by hand-written tests, not by the generator.

   `src/pg_compat/src/drivers_trsp.cpp` refuses a NULL or empty restriction path for
   pgr_turnRestrictedPath because `Rule::Rule` (`src/cpp_common/rule.cpp`) takes the path's last
   element and `turnRestrictedPath_driver.cpp` builds a Rule from every row. Re-read both at a
   bump: if upstream skips such rows there (as its TRSP drivers do with `if (r.via)`) or guards
   `Rule`, delete the guard.

   `src/pg_compat/src/drivers_graph.cpp` and `drivers_unified.cpp` answer two graphs with no rows
   instead of calling the driver: pgr_biconnectedComponents over self-loops only
   (`biconnectedComponents` in `src/components/components.cpp` indexes an empty vector) and
   pgr_sloanOrdering over vertices without edges (Boost's `sloan_ordering` crashes). Re-read both
   at a bump; if upstream guards them, delete the guard and its tests' comments.
6. Run `python3 scripts/check_signatures.py`. It reports signatures that upstream added, removed or
   retyped. Adapt the overload tables (`src/functions/*_specs.cpp`) for new, changed or
   removed overloads. For a new upstream function whose name collides with a DuckDB name, or which
   does not apply to DuckDB, add it to `test/pgrouting_not_ported.json` with a reason. Never rename
   a function to dodge a collision. If upstream's `include/cpp_common/path.hpp` now initializes
   `m_tot_cost` in its only_cost `Path` constructor, switch `pgr_dijkstraNearCost` back to
   `only_cost = true` with plain `Projection::COST` (see the `Projection::COST_OF_PATH`
   comment in `src/functions/function_spec.hpp`). Diff `src/exec/withpoints_keys.cpp` against the
   anonymous-namespace `get_new_queries` in
   `third_party/pgrouting/src/dijkstra/shortestPath_driver.cpp` and copy any change verbatim: those
   strings are the keys the driver looks the pgr_withPoints inputs up by. A drift shows up as every
   pgr_withPoints query failing with "no 'edges' input registered for query". Upstream keeps a
   second copy of the same template in `third_party/pgrouting/src/withPoints/get_new_queries.cpp`,
   which pgr_withPointsDD's C entry calls; `src/pg_compat/src/drivers_tree.cpp` passes the driver
   the same `WithPointsDerivedKeys` strings, so diff that copy too. pgr_withPointsKSP's,
   pgr_withPointsVia's, pgr_trsp_withPoints's and pgr_trspVia_withPoints's C entries call it as
   well, and `src/pg_compat/src/drivers_routes.cpp` and `drivers_trsp.cpp` pass their drivers the
   same strings.
7. Run the full suite: `make test_debug`, then push and let CI cover the remaining eight native
   platforms and the three Wasm variants.
8. The bump lands on `main` only. The `v2.0-cyanoptera` next line picks it up at its next
   rebase onto `main` (`docs/RELEASE.md`), where its three workflows must be green too, including
   the Checks step that keeps its tests identical to `main`'s. A compile error there only, not on
   `main`, means the new upstream code uses something the v2.0 adaptations do not cover.
9. Update `AGENTS.md` if the bump changed build commands, layout or conventions.
10. Users get the new pgRouting version only with a new release (`docs/RELEASE.md`).
