// SPDX-License-Identifier: GPL-2.0-or-later

// The per-family drivers whose rows are paths (ResultShape::PATH). Each case passes exactly the
// arguments its upstream C entry (src/<family>/*.c) passes.

#include "driver_groups.hpp"

#include "drivers/bdDijkstra/bdDijkstra_driver.h"
#include "drivers/bellman_ford/bellman_ford_driver.h"
#include "drivers/bellman_ford/edwardMoore_driver.h"
#include "drivers/breadthFirstSearch/binaryBreadthFirstSearch_driver.h"
#include "drivers/dagShortestPath/dagShortestPath_driver.h"

namespace duckdb_pgrouting {

bool CallPathDriver(const DriverRequest &request, const DriverArrays &arrays, DriverCall &call) {
	const char *edges = request.edges_sql.c_str();
	const char *combinations = CombinationsOrNull(request);
	switch (request.driver) {
	case DriverKind::BD_DIJKSTRA:
		pgr_do_bdDijkstra(edges, combinations, arrays.starts, arrays.ends, request.directed, request.only_cost,
		                  &call.path_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	case DriverKind::BELLMAN_FORD:
		pgr_do_bellman_ford(edges, combinations, arrays.starts, arrays.ends, request.directed, request.only_cost,
		                    &call.path_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	case DriverKind::EDWARD_MOORE:
		// No only_cost: pgr_edwardMoore has no Cost variant.
		pgr_do_edwardMoore(edges, combinations, arrays.starts, arrays.ends, request.directed, &call.path_rows,
		                   &call.count, &call.log, &call.notice, &call.err);
		return true;
	case DriverKind::DAG_SHORTEST_PATH:
		// No directed: the driver always builds a directed graph. normal is passed through.
		pgr_do_dagShortestPath(edges, combinations, arrays.starts, arrays.ends, request.only_cost, request.normal,
		                       &call.path_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	case DriverKind::BINARY_BFS:
		// No only_cost: pgr_binaryBreadthFirstSearch has no Cost variant.
		pgr_do_binaryBreadthFirstSearch(edges, combinations, arrays.starts, arrays.ends, request.directed,
		                                &call.path_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	default:
		return false;
	}
}

} // namespace duckdb_pgrouting
