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
	DRIVING_DISTANCE
};

// The upstream result struct a driver fills, one value per struct. It decides _pgr_exec's output
// columns (src/exec/result_emitters.cpp) and how DriverResult frees the rows.
enum class ResultShape : uint8_t {
	PATH,  // Path_rt
	PAIRS, // II_t_rt
	MST    // MST_rt
};

// A check upstream's C entry runs on the parameters before it calls the driver, even when the
// edge query returns nothing. _pgr_exec runs it at bind time, unless an argument is NULL: a
// STRICT function is never entered then.
enum class RequestCheck : uint8_t {
	NONE,
	// check_parameters(heuristic, factor, epsilon): src/common/check_parameters.c, called by
	// src/astar/astar.c and src/bdAstar/bdAstar.c.
	ASTAR_PARAMETERS,
	// distance >= 0: src/driving_distance/driving_distance.c.
	DRIVING_DISTANCE
};

struct DriverInfo {
	DriverKind kind;
	const char *name; // the spelling _pgr_exec's `driver` argument takes
	ResultShape shape;
	RequestCheck check;
};

// One row per DriverKind, in enumerator order: the only list of drivers.
inline constexpr DriverInfo DRIVERS[] = {
    {DriverKind::SHORTEST_PATH, "shortest_path", ResultShape::PATH, RequestCheck::NONE},
    {DriverKind::BD_DIJKSTRA, "bd_dijkstra", ResultShape::PATH, RequestCheck::NONE},
    {DriverKind::BELLMAN_FORD, "bellman_ford", ResultShape::PATH, RequestCheck::NONE},
    {DriverKind::EDWARD_MOORE, "edward_moore", ResultShape::PATH, RequestCheck::NONE},
    {DriverKind::DAG_SHORTEST_PATH, "dag_shortest_path", ResultShape::PATH, RequestCheck::NONE},
    {DriverKind::BINARY_BFS, "binary_bfs", ResultShape::PATH, RequestCheck::NONE},
    {DriverKind::CONNECTED_COMPONENTS, "connected_components", ResultShape::PAIRS, RequestCheck::NONE},
    {DriverKind::ASTAR, "astar", ResultShape::PATH, RequestCheck::ASTAR_PARAMETERS},
    {DriverKind::BD_ASTAR, "bd_astar", ResultShape::PATH, RequestCheck::ASTAR_PARAMETERS},
    {DriverKind::DRIVING_DISTANCE, "driving_distance", ResultShape::MST, RequestCheck::DRIVING_DISTANCE},
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
static_assert(DRIVER_COUNT == static_cast<std::size_t>(DriverKind::DRIVING_DISTANCE) + 1,
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
