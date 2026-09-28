// SPDX-License-Identifier: GPL-2.0-or-later

// The turn-restriction drivers: pgr_trsp's and pgr_trsp_withPoints's (Path_rt, ResultShape::PATH),
// pgr_trspVia's and pgr_trspVia_withPoints's (Routes_t, ResultShape::ROUTES) and
// pgr_turnRestrictedPath's (Path_rt, ResultShape::KSP). Every one reads the restrictions query
// (cost, path) besides its edges. Each case passes exactly the arguments its upstream C entry
// (src/trsp/*.c, src/ksp/turnRestrictedPath.c) passes.

#include "driver_groups.hpp"

#include "drivers/trsp/trsp_driver.h"
#include "drivers/trsp/trsp_withPoints_driver.h"
#include "drivers/trsp/trspVia_driver.h"
#include "drivers/trsp/trspVia_withPoints_driver.h"
#include "pgrouting/withpoints_keys.hpp"

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
	case DriverKind::TRSP_WITH_POINTS: {
		// get_new_queries (src/withPoints/get_new_queries.cpp) derives the two edge queries the
		// driver fetches; WithPointsDerivedKeys reproduces its template, so these are the keys the
		// derived inputs are registered under. The restrictions query comes second, as the C entry
		// passes it.
		const auto keys = WithPointsDerivedKeys(request.edges_sql, request.points_sql);
		pgr_do_trsp_withPoints(keys.no_points.c_str(), restrictions, request.points_sql.c_str(),
		                       keys.of_points.c_str(), combinations, arrays.starts, arrays.ends, request.directed,
		                       LowerDrivingSide(request), request.details, &call.path_rows, &call.count,
		                       &call.log, &call.notice, &call.err);
		return true;
	}
	case DriverKind::TRSP_VIA:
		pgr_do_trspVia(edges, restrictions, arrays.via, request.directed, request.strict, request.u_turn_on_edge,
		               &call.route_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	case DriverKind::TRSP_VIA_WITH_POINTS: {
		// The same derived edge queries as TRSP_WITH_POINTS; the side and details come before strict
		// and U_turn_on_edge in this driver's argument list.
		const auto keys = WithPointsDerivedKeys(request.edges_sql, request.points_sql);
		pgr_do_trspVia_withPoints(keys.no_points.c_str(), restrictions, request.points_sql.c_str(),
		                          keys.of_points.c_str(), arrays.via, request.directed, LowerDrivingSide(request),
		                          request.details, request.strict, request.u_turn_on_edge, &call.route_rows,
		                          &call.count, &call.log, &call.notice, &call.err);
		return true;
	}
	default:
		return false;
	}
}

} // namespace duckdb_pgrouting
