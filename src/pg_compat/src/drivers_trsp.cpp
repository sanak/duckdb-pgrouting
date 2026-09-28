// SPDX-License-Identifier: GPL-2.0-or-later

// The turn-restriction drivers: pgr_trsp's and pgr_trsp_withPoints's (Path_rt, ResultShape::PATH),
// pgr_trspVia's and pgr_trspVia_withPoints's (Routes_t, ResultShape::ROUTES) and
// pgr_turnRestrictedPath's (Path_rt, ResultShape::KSP). Every one reads the restrictions query
// (cost, path) besides its edges. Each case passes exactly the arguments its upstream C entry
// (src/trsp/*.c, src/ksp/turnRestrictedPath.c) passes.

#include "driver_groups.hpp"

#include "drivers/trsp/trsp_driver.h"

namespace duckdb_pgrouting {

bool CallTrspDriver(const DriverRequest &request, const DriverArrays &arrays, DriverCall &call) {
	const char *edges = request.edges_sql.c_str();
	const char *restrictions = request.restrictions_sql.c_str();
	const char *combinations = CombinationsOrNull(request);
	switch (request.driver) {
	case DriverKind::TRSP:
		pgr_do_trsp(edges, restrictions, combinations, arrays.starts, arrays.ends, request.directed,
		            &call.path_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	default:
		return false;
	}
}

} // namespace duckdb_pgrouting
