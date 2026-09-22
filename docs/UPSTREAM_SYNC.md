# Upstream sync procedure

pgRouting is a submodule pinned to a release tag and is never modified. Moving to a new tag is
the following checklist, in order. Every step either passes or tells you exactly what changed.

1. Bump `third_party/pgrouting` to the new tag and build (`GEN=ninja make release`). Compile
   errors are expected only in `src/exec` (a driver signature changed) or in `src/pg_compat` (a
   new PostgreSQL seam appeared). An error anywhere else means an upstream file joined the
   compiled set without being added to `cmake/pgrouting_sources.cmake`.
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
   - a diff in `test/pgrouting_ties.json`: an equal-cost tie now falls the other way. That is a
     Boost, vcpkg or row-order change, not a routing defect. Review it and commit it.
   - a non-zero exit with a `Mismatch`: this build's answer is not an equal-cost alternative to
     upstream's. Investigate before committing anything.
   Read the diff against upstream's release notes either way.
   Only each implemented function's own documentation page is processed; a generated file that
   is no longer produced (its function's page vanished upstream, or it emits nothing) is removed
   by a regenerating run and reported by `--check`.
5. Run `python3 scripts/check_signatures.py`. It reports signatures that upstream added, removed
   or retyped. Adapt the overload table in `src/functions/shortest_path_specs.cpp` for new,
   changed or removed overloads. For a new upstream function whose stripped name collides with a
   DuckDB name, or which does not apply to DuckDB, add it to `test/pgrouting_not_ported.json`
   with a reason. Never rename a function to dodge a collision. If upstream's
   `include/cpp_common/path.hpp` now initializes `m_tot_cost` in its only_cost `Path` constructor,
   switch `dijkstraNearCost` back to `only_cost = true` with plain `ResultColumns::COST` (see the
   `ResultColumns::COST_OF_PATH` comment in `src/functions/function_spec.hpp`).
   Diff `src/exec/withpoints_keys.cpp` against the anonymous-namespace `get_new_queries` in
   `third_party/pgrouting/src/dijkstra/shortestPath_driver.cpp` and copy any change verbatim:
   those strings are the keys the driver looks the withPoints inputs up by. A drift shows up as
   every withPoints query failing with "no 'edges' input registered for query".
6. Run the full suite: `make test_debug`, then push and let CI cover the remaining eight native
   platforms and the three Wasm variants.
7. Update `AGENTS.md` if the bump changed build commands, layout or conventions.
