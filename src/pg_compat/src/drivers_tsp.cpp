// SPDX-License-Identifier: GPL-2.0-or-later

// The traveling-salesperson drivers: pgr_TSP's, which reads a cost-matrix query (start_vid, end_vid,
// agg_cost), and pgr_TSPeuclidean's, which reads a coordinates query (id, x, y). Neither reads an
// edge query. Both fill TSP_tour_rt (ResultShape::TSP_TOUR) and take exactly what their C entries
// (src/tsp/TSP.c, src/tsp/euclideanTSP.c) pass: the query, start_id and end_id, where 0 means
// "not given".

#include "driver_groups.hpp"

#include "drivers/tsp/TSP_driver.h"

namespace duckdb_pgrouting {

bool CallTspDriver(const DriverRequest &request, const DriverArrays &, DriverCall &call) {
	switch (request.driver) {
	case DriverKind::TSP:
		pgr_do_tsp(request.matrix_sql.c_str(), request.start_id, request.end_id, &call.tour_rows, &call.count,
		           &call.log, &call.notice, &call.err);
		return true;
	default:
		return false;
	}
}

} // namespace duckdb_pgrouting
