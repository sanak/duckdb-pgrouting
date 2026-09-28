// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Everything a pgRouting driver needs, in this extension's own vocabulary. Includes neither DuckDB
// nor the compat postgres.h, so the exec function and the pg_compat adapter both read it.

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "pgrouting/driver_kind.hpp"

namespace duckdb_pgrouting {

struct DriverRequest {
	std::string edges_sql;
	std::string points_sql;
	std::string combinations_sql;
	// The turn-restriction families' restrictions query (cost, path).
	std::string restrictions_sql;
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
	// Upstream's default is BIGINT's maximum: no depth limit.
	int64_t max_depth = std::numeric_limits<int64_t>::max();
	// The kruskal and prim families: "" (the whole forest), "BFS", "DFS" or "DD".
	std::string mst_suffix;
	// The K-shortest-path families (pgr_ksp, pgr_withPointsKSP) and pgr_turnRestrictedPath; K is
	// upstream's INTEGER.
	int32_t k = 0;
	bool heap_paths = false;
	// pgr_turnRestrictedPath: stop at the first path that breaks no restriction.
	bool stop_on_first = true;
	// The Via families (pgr_dijkstraVia, pgr_withPointsVia, pgr_trspVia, pgr_trspVia_withPoints).
	// via travels in the input row like starts. pgr_turnRestrictedPath reads strict too.
	std::vector<int64_t> via;
	bool has_via = false;
	bool strict = false;
	bool u_turn_on_edge = true;
	// Which driver runs the request. Each driver reads only the fields its upstream C entry passes
	// it; only SHORTEST_PATH reads points_sql, n_goals, global, driving_side, details and which,
	// and the other drivers that take points (DriverInfo::takes_points) also read points_sql,
	// driving_side and details.
	DriverKind driver = DriverKind::SHORTEST_PATH;
};

} // namespace duckdb_pgrouting
