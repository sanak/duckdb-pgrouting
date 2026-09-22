// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// The registry keys of the two inputs pgRouting derives when points are given. Includes neither
// DuckDB nor the compat postgres.h, like input_access.hpp.

#include <string>

namespace duckdb_pgrouting {

struct WithPointsKeys {
	std::string of_points; // edges that carry at least one point
	std::string no_points; // every other edge
};

WithPointsKeys WithPointsDerivedKeys(const std::string &edges_sql, const std::string &points_sql);

} // namespace duckdb_pgrouting
