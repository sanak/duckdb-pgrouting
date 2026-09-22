# duckdb-routing

A DuckDB extension that brings [pgRouting](https://pgrouting.org/)'s graph algorithms to DuckDB,
with pgRouting's SQL API minus the `pgr_` prefix. pgRouting's C++ code is reused unmodified and
statically linked; only its PostgreSQL-specific layers are replaced.

Status: early development — implemented so far: `dijkstra`, `dijkstraCost`, `dijkstraCostMatrix`,
`dijkstraNear` and `dijkstraNearCost`.

## Building

Clone with submodules (`git clone --recurse-submodules …`, or run
`git submodule update --init --recursive` in an existing clone), then see `AGENTS.md` for the
prerequisites and the build and test commands.

License: GPL-2.0-or-later (see `LICENSE`), because pgRouting is GPL-2.0-or-later.
