// SPDX-License-Identifier: GPL-2.0-or-later

#include "routing/withpoints_keys.hpp"

namespace duckdb_routing {

// A character-for-character copy of get_new_queries in the anonymous namespace of
// third_party/pgrouting/src/dijkstra/shortestPath_driver.cpp. do_shortestPath calls that function
// to build the two SQL strings it then fetches, and it has internal linkage, so it can be neither
// called nor replaced from here. The strings are only registry keys: the rows behind them are
// built by DuckDB from an equivalent parsed query (shortest_path_functions.cpp). If upstream
// changes its template, every withPoints query fails with "no 'edges' input registered for query"
// naming upstream's new string; docs/UPSTREAM_SYNC.md makes diffing the two a step of every bump.
WithPointsKeys WithPointsDerivedKeys(const std::string &edges_sql, const std::string &points_sql) {
	WithPointsKeys keys;
	// clang-format off
	keys.of_points = std::string("WITH ")
	    + " edges AS (" + edges_sql + "), "
	    + " points AS (" + points_sql + ")"
	    + " SELECT DISTINCT edges.* FROM edges JOIN points ON (id = edge_id)";

	keys.no_points = std::string("WITH ")
	    + " edges AS (" + edges_sql + "), "
	    + " points AS (" + points_sql + ")"
	    + " SELECT edges.*"
	    + " FROM edges"
	    + " WHERE NOT EXISTS (SELECT edge_id FROM points WHERE id = edge_id)";
	// clang-format on
	return keys;
}

} // namespace duckdb_routing
