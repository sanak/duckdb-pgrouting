# duckdb-pgrouting

A DuckDB extension that brings [pgRouting](https://pgrouting.org/)'s graph algorithms to DuckDB,
with pgRouting's own SQL API, `pgr_` function names included. pgRouting's C++ code is reused unmodified and
statically linked; only its PostgreSQL-specific layers are replaced.

Status: early development — implemented so far: `pgr_dijkstra`, `pgr_dijkstraCost`, `pgr_dijkstraCostMatrix`,
`pgr_dijkstraNear`, `pgr_dijkstraNearCost`, `pgr_withPoints`, `pgr_withPointsCost`, `pgr_withPointsCostMatrix`,
`pgr_bdDijkstra`, `pgr_bdDijkstraCost`, `pgr_bdDijkstraCostMatrix`, `pgr_bellmanFord`, `pgr_edwardMoore`,
`pgr_dagShortestPath` and `pgr_binaryBreadthFirstSearch`.

## Building

Clone with submodules (`git clone --recurse-submodules …`, or run
`git submodule update --init --recursive` in an existing clone), then see `AGENTS.md` for the
prerequisites and the build and test commands.

License: GPL-2.0-or-later (see `LICENSE`), because pgRouting is GPL-2.0-or-later.
