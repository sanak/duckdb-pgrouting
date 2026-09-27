// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Everything a pgRouting driver needs, in this extension's own vocabulary. Includes neither DuckDB
// nor the compat postgres.h, so the exec function and the pg_compat adapter both read it.

#include <cstdint>
#include <string>
#include <vector>

#include "pgrouting/driver_kind.hpp"

namespace duckdb_pgrouting {

struct DriverRequest {
	std::string edges_sql;
	std::string points_sql;
	std::string combinations_sql;
	std::vector<int64_t> starts;
	std::vector<int64_t> ends;
	bool has_starts = false;
	bool has_ends = false;
	bool directed = true;
	bool only_cost = false;
	bool normal = true;
	int64_t n_goals = 0;
	bool global = false;
	char driving_side = ' ';
	bool details = true;
	int32_t which = 0;
	// The A* families (pgr_aStar*, pgr_bdAstar*), with upstream's defaults.
	int32_t heuristic = 5;
	double factor = 1.0;
	double epsilon = 1.0;
	// The root-based families (pgr_drivingDistance, pgr_withPointsDD, the kruskal and prim families,
	// pgr_breadthFirstSearch, pgr_depthFirstSearch). roots travels in the input row like starts.
	std::vector<int64_t> roots;
	bool has_roots = false;
	double distance = 0;
	bool equicost = false;
	// Which driver runs the request. Each driver reads only the fields its upstream C entry passes
	// it; only SHORTEST_PATH reads points_sql, n_goals, global, driving_side, details and which.
	DriverKind driver = DriverKind::SHORTEST_PATH;
};

} // namespace duckdb_pgrouting
