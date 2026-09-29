// SPDX-License-Identifier: GPL-2.0-or-later

// The flow drivers: maximum flow (pgr_pushRelabel, pgr_boykovKolmogorov, pgr_edmondsKarp,
// pgr_maxFlow), minimum-cost maximum flow, edge-disjoint paths, maximum cardinality matching and
// the Chinese Postman. Each case passes exactly the arguments its upstream C entry
// (src/max_flow/*.c, src/chinese/chinesePostman.c) passes.

#include "driver_groups.hpp"

#include "drivers/max_flow/max_flow_driver.h"

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
	default:
		return false;
	}
}

} // namespace duckdb_pgrouting
