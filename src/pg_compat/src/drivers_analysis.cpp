// SPDX-License-Identifier: GPL-2.0-or-later

// The per-family drivers of the structural graph-analysis functions: planarity, line graphs,
// transitive closure, dominator tree, circuits and minimum cut. Each case passes exactly the
// arguments its upstream C entry (src/planar/isPlanar.c, src/lineGraph/*.c,
// src/transitiveClosure/transitiveClosure.c, src/dominator/lengauerTarjanDominatorTree.c,
// src/circuits/hawickCircuits.c, src/mincut/stoerWagner.c) passes.

#include "driver_groups.hpp"

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
	default:
		return false;
	}
}

} // namespace duckdb_pgrouting
