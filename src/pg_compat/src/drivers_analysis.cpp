// SPDX-License-Identifier: GPL-2.0-or-later

// The per-family drivers of the structural graph-analysis functions: planarity, line graphs,
// transitive closure, dominator tree, circuits and minimum cut. Each case passes exactly the
// arguments its upstream C entry (src/planar/isPlanar.c, src/lineGraph/*.c,
// src/transitiveClosure/transitiveClosure.c, src/dominator/lengauerTarjanDominatorTree.c,
// src/circuits/hawickCircuits.c, src/mincut/stoerWagner.c) passes.

#include "driver_groups.hpp"

#include "drivers/lineGraph/lineGraphFull_driver.h"
#include "drivers/lineGraph/lineGraph_driver.h"
#include "drivers/planar/isPlanar_driver.h"

namespace duckdb_pgrouting {

bool CallAnalysisDriver(const DriverRequest &request, const DriverArrays &, DriverCall &call) {
	const char *edges = request.edges_sql.c_str();
	switch (request.driver) {
	case DriverKind::IS_PLANAR: {
		// The C entry returns the driver's bool. An empty edge query is false, with no message.
		const bool planar = pgr_do_isPlanar(edges, &call.log, &call.notice, &call.err);
		if (call.err == nullptr) {
			call.id_rows = SingleIdRow(planar ? 1 : 0);
			call.count = 1;
		}
		return true;
	}
	case DriverKind::LINE_GRAPH:
		pgr_do_lineGraph(edges, request.directed, &call.edge_rows, &call.count, &call.log, &call.notice,
		                 &call.err);
		return true;
	case DriverKind::LINE_GRAPH_FULL:
		// The driver writes its whole line graph into the log (a debugging `#if 1` upstream), which
		// reaches DUCKDB_LOG_DEBUG like every driver log.
		pgr_do_lineGraphFull(edges, &call.line_graph_full_rows, &call.count, &call.log, &call.notice,
		                     &call.err);
		return true;
	default:
		return false;
	}
}

} // namespace duckdb_pgrouting
