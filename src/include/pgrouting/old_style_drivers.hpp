// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// The seam to pgRouting's per-family drivers. Their own headers include postgres.h, so they are
// called from one pg_compat-side translation unit (src/pg_compat/src/old_style_drivers.cpp); this
// header includes neither DuckDB nor the compat postgres.h, like input_access.hpp.

#include <cstddef>
#include <string>

#include "pgrouting/driver_kind.hpp"

struct ArrayType;
struct II_t_rt;
struct Path_rt;

namespace duckdb_pgrouting {

struct OldStyleOutput {
	Path_rt *rows = nullptr;  // allocated by the driver with malloc; the caller takes ownership
	II_t_rt *pairs = nullptr; // CONNECTED_COMPONENTS only, in place of rows; same ownership
	std::size_t count = 0;    // of whichever of the two arrays is set
	std::string log;
	std::string notice;
	std::string err;
};

// Runs one per-family driver. `kind` must not be SHORTEST_PATH. An empty combinations_sql means
// "array form". `directed` is ignored by DAG_SHORTEST_PATH, `only_cost` by EDWARD_MOORE and
// BINARY_BFS, and `normal` by every kind but DAG_SHORTEST_PATH: those drivers take no such
// parameter. CONNECTED_COMPONENTS reads only edges_sql and fills `pairs` rather than `rows`.
OldStyleOutput RunOldStyle(DriverKind kind, const std::string &edges_sql, const std::string &combinations_sql,
                           ArrayType *starts, ArrayType *ends, bool directed, bool only_cost, bool normal);

} // namespace duckdb_pgrouting
