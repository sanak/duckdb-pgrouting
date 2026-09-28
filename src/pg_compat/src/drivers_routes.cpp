// SPDX-License-Identifier: GPL-2.0-or-later

// The per-family drivers that return several paths per request: the K shortest paths between each
// start and end (Path_rt, ResultShape::KSP) and routes through a list of vertices (Routes_t,
// ResultShape::ROUTES). Each case passes exactly the arguments its upstream C entry
// (src/<family>/*.c) passes.

#include "driver_groups.hpp"

#include "drivers/yen/ksp_driver.h"

namespace duckdb_pgrouting {

bool CallRoutesDriver(const DriverRequest &request, const DriverArrays &arrays, DriverCall &call) {
	const char *edges = request.edges_sql.c_str();
	const char *combinations = CombinationsOrNull(request);
	switch (request.driver) {
	case DriverKind::KSP:
		// _pgr_ksp_v4 passes the id arrays and no single start/end vertex. A negative K never gets
		// here (CheckRequest answers it with no rows, as src/ksp/ksp.c does), so the cast is safe.
		pgr_do_ksp(edges, combinations, arrays.starts, arrays.ends, nullptr, nullptr,
		           static_cast<size_t>(request.k), request.directed, request.heap_paths, &call.path_rows,
		           &call.count, &call.log, &call.notice, &call.err);
		return true;
	default:
		return false;
	}
}

} // namespace duckdb_pgrouting
