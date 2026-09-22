// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Which pgRouting driver an overload runs. Includes neither DuckDB nor the compat postgres.h: the
// spec table, the exec function and the pg_compat-side adapter all use it.

#include <cstdint>
#include <initializer_list>
#include <string>

namespace duckdb_routing {

// SHORTEST_PATH is pgRouting's unified do_shortestPath. The others are the per-family drivers that
// pgRouting v4.0.2 still keeps; old_style_drivers.cpp calls them.
enum class DriverKind : uint8_t {
	SHORTEST_PATH,
	BD_DIJKSTRA,
	BELLMAN_FORD,
	EDWARD_MOORE,
	DAG_SHORTEST_PATH,
	BINARY_BFS
};

// The spelling _pgr_shortestpath_exec's `driver` argument takes.
inline const char *DriverKindName(DriverKind kind) {
	switch (kind) {
	case DriverKind::SHORTEST_PATH:
		return "shortest_path";
	case DriverKind::BD_DIJKSTRA:
		return "bd_dijkstra";
	case DriverKind::BELLMAN_FORD:
		return "bellman_ford";
	case DriverKind::EDWARD_MOORE:
		return "edward_moore";
	case DriverKind::DAG_SHORTEST_PATH:
		return "dag_shortest_path";
	case DriverKind::BINARY_BFS:
		return "binary_bfs";
	}
	return "shortest_path";
}

inline bool ParseDriverKind(const std::string &name, DriverKind &kind) {
	for (auto candidate : {DriverKind::SHORTEST_PATH, DriverKind::BD_DIJKSTRA, DriverKind::BELLMAN_FORD,
	                       DriverKind::EDWARD_MOORE, DriverKind::DAG_SHORTEST_PATH, DriverKind::BINARY_BFS}) {
		if (name == DriverKindName(candidate)) {
			kind = candidate;
			return true;
		}
	}
	return false;
}

} // namespace duckdb_routing
