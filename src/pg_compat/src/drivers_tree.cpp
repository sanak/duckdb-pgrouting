// SPDX-License-Identifier: GPL-2.0-or-later

// The per-family drivers whose rows are trees (ResultShape::MST): every vertex reached from one or
// more roots, with its predecessor, depth and cost. Each case passes exactly the arguments its
// upstream C entry (src/<family>/*.c) passes.

#include "driver_groups.hpp"

#include "drivers/driving_distance/driving_distance_driver.h"

namespace duckdb_pgrouting {

bool CallTreeDriver(const DriverRequest &request, const DriverArrays &arrays, DriverCall &call) {
	const char *edges = request.edges_sql.c_str();
	switch (request.driver) {
	case DriverKind::DRIVING_DISTANCE:
		pgr_do_drivingDistance(edges, arrays.roots, request.distance, request.directed, request.equicost,
		                       &call.mst_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	default:
		return false;
	}
}

} // namespace duckdb_pgrouting
