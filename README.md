# duckdb-routing

A DuckDB extension that brings [pgRouting](https://pgrouting.org/)'s graph algorithms to DuckDB,
with pgRouting's SQL API minus the `pgr_` prefix. pgRouting's C++ code is reused unmodified and
statically linked; only its PostgreSQL-specific layers are replaced.

Status: early development — the extension loads but exposes no functions yet.

License: GPL-2.0-or-later (see `LICENSE`), because pgRouting is GPL-2.0-or-later.
