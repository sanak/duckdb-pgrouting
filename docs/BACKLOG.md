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
- **`duckdbcli.query()` starts two subprocesses per query** — one for `DESCRIBE`, one for the query
  itself — and each one reruns the whole SQL preamble, which now loads five CSV fixtures. Across
  the roughly 46 blocks the dijkstra docqueries generator runs, that is on the order of 90-some
  process starts, and `subprocess.run` is called with no timeout either. Acceptable for a tool that
  only runs at regeneration time, not in any inner loop a developer waits on repeatedly.

## Closed as declined

- **A routing-specific log tag.** `DUCKDB_LOG_INFO` has no tag parameter, and adding one needs a
  `LogType` class plus the `DUCKDB_LOG` macro. Routing messages are therefore not filterable by tag
  name in `duckdb_logs`. They are still visible: they arrive with an empty `type` and the level
  upstream chose — `INFO` for a notice, `DEBUG` for a log line — which is what
  `test/sql/dijkstra_errors.test` asserts against. Reopen only if something needs tag-based
  filtering for its own sake.

## Test tooling gaps

- **The docqueries generator has no skip reason for a block whose SQL simply cannot run against the
  sample fixtures.** Its skip reasons cover an unimplemented function, an empty result set, more
  than one result set, and a blank cell in a text column — but not a query that fails to bind. This
  is why regeneration is currently run scoped to `--category dijkstra`: several other upstream
  categories query columns the five-table sample fixture does not carry (for example
  `contraction`'s plain `SELECT id, is_contracted FROM vertices`, which has no `pgr_` call for the
  translator to touch and so reaches the database unchanged), and the generator aborts on the first
  one instead of skipping it. Widening the regenerated category set means adding that fifth skip
  reason first.
- **Regenerating one category currently rewrites the whole ties file.** `gen_docqueries_tests.py`'s
  `main` writes `test/pgrouting_ties.json` from only the categories it just processed, so once a
  second category with tie-classified blocks exists, regenerating either one on its own would drop
  the other's entries. Harmless today because `dijkstra` is the only category with tie-classified
  blocks, and CI's category-scoped `--check` would still catch the loss loudly (as a diff) rather
  than silently. The real fix is to merge newly generated ties into the file's existing contents
  instead of replacing them, for the categories a given run did not touch.

## Unreachable today

Each of these would be a change no test could observe, so none of them is made:

- `duckdb_routing::ColumnClass::CHAR1` in `src/include/routing/input_access.hpp` is never produced
  by `ClassOf`, so the `CHAR1` arm of `Accepts` in `src/pg_compat/src/get_check_data.cpp` cannot
  fire.
- `getText` in `src/pg_compat/src/get_check_data.cpp` returns a `std::malloc`'d buffer with no
  owner. Unreachable on the dijkstra path, which fetches no TEXT column.
- `seq[i] = NumericCast<int32_t>(k + 1)` in `src/exec/shortest_path_exec.cpp` throws past 2^31
  result rows. Upstream uses `int` for `seq` too, so matching it is the deliberate choice.
- `TagFunctions` in `src/functions/shortest_path_functions.cpp` resolves the same catalog entry
  once per spec, so two specs disagreeing on `pgrouting_name` for one public name would resolve
  silently to the first.
- The "No elements found" text in `get_pgarray` (`src/pg_compat/src/get_check_data.cpp`) has no
  test, because no registered dijkstra overload reaches it: an empty id list, for example
  `dijkstra(sql, []::BIGINT[], 3)`, returns zero rows rather than raising.
- **A list-typed result column would never compare equal between upstream and this build.** Upstream
  renders one as `{4,7}` in its psql transcript, while a DuckDB list stringifies as `[4, 7]`. No
  `dijkstra` documentation query returns a list-typed column today, so nothing exercises this, but
  the first category that does will meet it and the comparison in `gen_docqueries_tests.py` will
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
- **Two of the six stems in the `dijkstra` category, `dijkstraCostMatrix` and `dijkstraVia`,
  produce no generated test file at all.** Every block in each one calls a function this extension
  does not implement (`pgr_dijkstraCostMatrix` and `pgr_TSP` for the former, `pgr_dijkstravia` for
  the latter), so the generator has nothing left to write once the unimplemented ones are skipped,
  and it does not emit a file with zero assertions. The category itself still has generated files —
  `dijkstra.test`, `dijkstraCost.test`, `dijkstraNear.test` and `dijkstraNearCost.test` all exist —
  only these two stems within it have none. Coverage for "what upstream offers here that this build
  does not" is recorded by layer 4 (the signature comparison) for all three functions, and by
  `test/pgrouting_not_ported.json` only for `pgr_dijkstravia`; the other two surface solely through
  `check_signatures.py`'s unimplemented-function list.

## Open decision

- **`ListOfRows` and `ORDER BY`: performance against run-to-run reproducibility.** `ListOfRows` in
  `src/functions/shortest_path_functions.cpp` emits `(SELECT list(_pgr_row) FROM (<sql>)
  _pgr_row)`. DuckDB's `list()` does not preserve scan order once the scan runs on several threads,
  and Boost breaks an equal-cost predecessor tie by adjacency-list insertion order, so on a large
  enough edge set the same query on the same data can return different — equally optimal — routes
  between runs. Adding `ORDER BY _pgr_row` would buy reproducibility at the price of a sort over
  the whole edge set on every query, and DuckDB struct ordering does not cover every child type, so
  it would need verification of its own. It may belong behind a setting rather than as a default.
  Not decided.
