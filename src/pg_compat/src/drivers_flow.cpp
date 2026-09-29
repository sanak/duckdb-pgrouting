// SPDX-License-Identifier: GPL-2.0-or-later

// The flow drivers: maximum flow (pgr_pushRelabel, pgr_boykovKolmogorov, pgr_edmondsKarp,
// pgr_maxFlow), minimum-cost maximum flow, edge-disjoint paths, maximum cardinality matching and
// the Chinese Postman. Each case passes exactly the arguments its upstream C entry
// (src/max_flow/*.c, src/chinese/chinesePostman.c) passes.

#include "driver_groups.hpp"

#include "drivers/max_flow/edge_disjoint_paths_driver.h"
#include "drivers/max_flow/max_flow_driver.h"
#include "drivers/max_flow/maximum_cardinality_matching_driver.h"
#include "drivers/max_flow/minCostMaxFlow_driver.h"

namespace duckdb_pgrouting {

bool CallFlowDriver(const DriverRequest &request, const DriverArrays &arrays, DriverCall &call) {
	const char *edges = request.edges_sql.c_str();
	const char *combinations = CombinationsOrNull(request);
	switch (request.driver) {
	case DriverKind::MAX_FLOW:
		// only_cost stands for upstream's only_flow: pgr_maxFlow's single row carries the total.
		pgr_do_max_flow(edges, combinations, arrays.starts, arrays.ends, request.algorithm, request.only_cost,
		                &call.flow_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	case DriverKind::MIN_COST_MAX_FLOW:
		pgr_do_minCostMaxFlow(edges, combinations, arrays.starts, arrays.ends, request.only_cost, &call.flow_rows,
		                      &call.count, &call.log, &call.notice, &call.err);
		return true;
	case DriverKind::EDGE_DISJOINT_PATHS:
		// Each path ends with a row of edge -1. src/max_flow/edge_disjoint_paths.c numbers the rows as
		// the K-shortest-path C entries do (EmitKsp), except that it restarts path_seq only after edge
		// -1, which differs only for negative edge ids of the caller's own.
		pgr_do_edge_disjoint_paths(edges, combinations, arrays.starts, arrays.ends, request.directed,
		                           &call.path_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	case DriverKind::MAX_CARDINALITY_MATCH:
		// v4.0 has no directed: the graph is undirected, and the rows are the matched edge ids, sorted.
		pgr_do_maximum_cardinality_matching(edges, &call.id_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	default:
		return false;
	}
}

} // namespace duckdb_pgrouting
