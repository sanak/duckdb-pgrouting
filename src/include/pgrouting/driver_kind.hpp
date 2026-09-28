// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Which pgRouting driver an overload runs, and what that driver returns. Includes neither DuckDB
// nor the compat postgres.h: the spec tables, the exec function and the pg_compat-side adapter all
// use it.

#include <cstddef>
#include <cstdint>
#include <string>

namespace duckdb_pgrouting {

// SHORTEST_PATH is pgRouting's unified do_shortestPath. The others are the per-family drivers that
// pgRouting v4.0.2 still keeps; the pg_compat adapter (src/pg_compat/src/family_drivers.cpp) calls
// them.
enum class DriverKind : uint8_t {
	SHORTEST_PATH,
	BD_DIJKSTRA,
	BELLMAN_FORD,
	EDWARD_MOORE,
	DAG_SHORTEST_PATH,
	BINARY_BFS,
	CONNECTED_COMPONENTS,
	ASTAR,
	BD_ASTAR,
	DRIVING_DISTANCE,
	WITH_POINTS_DD,
	KRUSKAL,
	PRIM,
	BREADTH_FIRST_SEARCH,
	DEPTH_FIRST_SEARCH,
	KSP,
	WITH_POINTS_KSP,
	DIJKSTRA_VIA,
	WITH_POINTS_VIA,
	TRSP,
	TRSP_WITH_POINTS,
	TRSP_VIA,
	TRSP_VIA_WITH_POINTS,
	TURN_RESTRICTED_PATH
};

// The upstream result struct a driver fills, and how its C entry numbers the rows. It decides
// _pgr_exec's output columns (src/exec/result_emitters.cpp) and how DriverResult frees the rows.
enum class ResultShape : uint8_t {
	PATH,  // Path_rt
	PAIRS, // II_t_rt
	MST,   // MST_rt
	KSP,   // Path_rt, numbered as upstream's K-shortest-path C entries number it (path_id)
	ROUTES // Routes_t
};

// A check upstream's C entry runs on the parameters before it calls the driver, even when the
// edge query returns nothing. _pgr_exec runs it at bind time, unless an argument is NULL: a
// STRICT function is never entered then. Some checks make the C entry return no rows instead of
// raising; CheckRequest then reports that the driver is not called.
enum class RequestCheck : uint8_t {
	NONE,
	// check_parameters(heuristic, factor, epsilon): src/common/check_parameters.c, called by
	// src/astar/astar.c and src/bdAstar/bdAstar.c.
	ASTAR_PARAMETERS,
	// distance >= 0: src/driving_distance/driving_distance.c.
	DRIVING_DISTANCE,
	// estimate_drivingSide (r, l or b in either case), then distance >= 0:
	// src/driving_distance/driving_distance_withPoints.c.
	WITH_POINTS_DD,
	// src/spanningTree/kruskal.c and prim.c: distance >= 0 with the DD suffix, max_depth >= 0
	// with BFS or DFS, nothing without a suffix (pgr_kruskal passes -1 for both).
	SPANNING_TREE,
	// max_depth >= 0: src/breadthFirstSearch/breadthFirstSearch.c, src/traversal/depthFirstSearch.c.
	TRAVERSAL,
	// K < 0 returns no rows, silently, before the driver is called: src/ksp/ksp.c (its "TODO
	// return error message").
	KSP_K,
	// estimate_drivingSide (r, l or b in either case), then K >= 0: src/ksp/withPoints_ksp.c.
	WITH_POINTS_KSP,
	// estimate_drivingSide (r, l or b in either case): src/withPoints/withPointsVia.c,
	// src/trsp/trsp_withPoints.c and src/trsp/trspVia_withPoints.c.
	DRIVING_SIDE,
	// K >= 0: src/ksp/turnRestrictedPath.c, which raises where src/ksp/ksp.c returns nothing.
	TURN_RESTRICTED_PATH
};

struct DriverInfo {
	DriverKind kind;
	const char *name; // the spelling _pgr_exec's `driver` argument takes
	ResultShape shape;
	RequestCheck check;
	// Whether the driver reads points_sql and the two edge queries derived from it.
	bool takes_points = false;
};

// One row per DriverKind, in enumerator order: the only list of drivers.
inline constexpr DriverInfo DRIVERS[] = {
    {DriverKind::SHORTEST_PATH, "shortest_path", ResultShape::PATH, RequestCheck::NONE, true},
    {DriverKind::BD_DIJKSTRA, "bd_dijkstra", ResultShape::PATH, RequestCheck::NONE},
    {DriverKind::BELLMAN_FORD, "bellman_ford", ResultShape::PATH, RequestCheck::NONE},
    {DriverKind::EDWARD_MOORE, "edward_moore", ResultShape::PATH, RequestCheck::NONE},
    {DriverKind::DAG_SHORTEST_PATH, "dag_shortest_path", ResultShape::PATH, RequestCheck::NONE},
    {DriverKind::BINARY_BFS, "binary_bfs", ResultShape::PATH, RequestCheck::NONE},
    {DriverKind::CONNECTED_COMPONENTS, "connected_components", ResultShape::PAIRS, RequestCheck::NONE},
    {DriverKind::ASTAR, "astar", ResultShape::PATH, RequestCheck::ASTAR_PARAMETERS},
    {DriverKind::BD_ASTAR, "bd_astar", ResultShape::PATH, RequestCheck::ASTAR_PARAMETERS},
    {DriverKind::DRIVING_DISTANCE, "driving_distance", ResultShape::MST, RequestCheck::DRIVING_DISTANCE},
    {DriverKind::WITH_POINTS_DD, "with_points_dd", ResultShape::MST, RequestCheck::WITH_POINTS_DD, true},
    {DriverKind::KRUSKAL, "kruskal", ResultShape::MST, RequestCheck::SPANNING_TREE},
    {DriverKind::PRIM, "prim", ResultShape::MST, RequestCheck::SPANNING_TREE},
    {DriverKind::BREADTH_FIRST_SEARCH, "breadth_first_search", ResultShape::MST, RequestCheck::TRAVERSAL},
    {DriverKind::DEPTH_FIRST_SEARCH, "depth_first_search", ResultShape::MST, RequestCheck::TRAVERSAL},
    {DriverKind::KSP, "ksp", ResultShape::KSP, RequestCheck::KSP_K},
    {DriverKind::WITH_POINTS_KSP, "with_points_ksp", ResultShape::KSP, RequestCheck::WITH_POINTS_KSP, true},
    {DriverKind::DIJKSTRA_VIA, "dijkstra_via", ResultShape::ROUTES, RequestCheck::NONE},
    {DriverKind::WITH_POINTS_VIA, "with_points_via", ResultShape::ROUTES, RequestCheck::DRIVING_SIDE, true},
    {DriverKind::TRSP, "trsp", ResultShape::PATH, RequestCheck::NONE},
    {DriverKind::TRSP_WITH_POINTS, "trsp_with_points", ResultShape::PATH, RequestCheck::DRIVING_SIDE, true},
    {DriverKind::TRSP_VIA, "trsp_via", ResultShape::ROUTES, RequestCheck::NONE},
    {DriverKind::TRSP_VIA_WITH_POINTS, "trsp_via_with_points", ResultShape::ROUTES, RequestCheck::DRIVING_SIDE, true},
    {DriverKind::TURN_RESTRICTED_PATH, "turn_restricted_path", ResultShape::KSP, RequestCheck::TURN_RESTRICTED_PATH},
};

inline constexpr std::size_t DRIVER_COUNT = sizeof(DRIVERS) / sizeof(DRIVERS[0]);

constexpr bool DriversInEnumeratorOrder() {
	for (std::size_t i = 0; i < DRIVER_COUNT; i++) {
		if (static_cast<std::size_t>(DRIVERS[i].kind) != i) {
			return false;
		}
	}
	return true;
}
static_assert(DriversInEnumeratorOrder(), "DRIVERS must list every DriverKind once, in enumerator order");
// Update to the last enumerator whenever one is added.
static_assert(DRIVER_COUNT == static_cast<std::size_t>(DriverKind::TURN_RESTRICTED_PATH) + 1,
              "DRIVERS must have a row for every DriverKind");

inline const DriverInfo &InfoOf(DriverKind kind) {
	return DRIVERS[static_cast<std::size_t>(kind)];
}

// nullptr when no driver has that name.
inline const DriverInfo *FindDriver(const std::string &name) {
	for (const auto &info : DRIVERS) {
		if (name == info.name) {
			return &info;
		}
	}
	return nullptr;
}

} // namespace duckdb_pgrouting
