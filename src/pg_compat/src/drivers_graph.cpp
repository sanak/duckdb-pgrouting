// SPDX-License-Identifier: GPL-2.0-or-later

// The per-family drivers whose rows are not paths.

#include "driver_groups.hpp"

#include "drivers/components/connectedComponents_driver.h"

namespace duckdb_pgrouting {

bool CallGraphDriver(const DriverRequest &request, const DriverArrays &, DriverCall &call) {
	switch (request.driver) {
	case DriverKind::CONNECTED_COMPONENTS:
		// Upstream's develop branch replaces this driver with do_coloring(..., CONNECTEDCOMPONENTS);
		// at the pgRouting bump that brings it, call that here instead.
		pgr_do_connectedComponents(request.edges_sql.c_str(), &call.pair_rows, &call.count, &call.log,
		                           &call.notice, &call.err);
		return true;
	default:
		return false;
	}
}

} // namespace duckdb_pgrouting
