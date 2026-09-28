// SPDX-License-Identifier: GPL-2.0-or-later

// The per-family drivers that return several paths per request: the K shortest paths between each
// start and end (Path_rt, ResultShape::KSP) and routes through a list of vertices (Routes_t,
// ResultShape::ROUTES). Each case passes exactly the arguments its upstream C entry
// (src/<family>/*.c) passes.

#include "driver_groups.hpp"

#include "drivers/dijkstra/dijkstraVia_driver.h"
#include "drivers/withPoints/withPointsVia_driver.h"
#include "drivers/yen/ksp_driver.h"
#include "drivers/yen/withPoints_ksp_driver.h"
#include "pgrouting/withpoints_keys.hpp"

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
	case DriverKind::WITH_POINTS_KSP: {
		// get_new_queries (src/withPoints/get_new_queries.cpp) derives the two edge queries the
		// driver fetches; WithPointsDerivedKeys reproduces its template, so these are the keys the
		// derived inputs are registered under.
		const auto keys = WithPointsDerivedKeys(request.edges_sql, request.points_sql);
		pgr_do_withPointsKsp(keys.no_points.c_str(), request.points_sql.c_str(), keys.of_points.c_str(),
		                     combinations, arrays.starts, arrays.ends, nullptr, nullptr,
		                     static_cast<size_t>(request.k), request.directed, request.heap_paths,
		                     LowerDrivingSide(request), request.details, &call.path_rows, &call.count,
		                     &call.log, &call.notice, &call.err);
		return true;
	}
	case DriverKind::DIJKSTRA_VIA:
		pgr_do_dijkstraVia(edges, arrays.via, request.directed, request.strict, request.u_turn_on_edge,
		                   &call.route_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	case DriverKind::WITH_POINTS_VIA: {
		// The same derived edge queries as WITH_POINTS_KSP; the side comes before details, strict and
		// U_turn_on_edge in this driver's argument list.
		const auto keys = WithPointsDerivedKeys(request.edges_sql, request.points_sql);
		pgr_do_withPointsVia(keys.no_points.c_str(), request.points_sql.c_str(), keys.of_points.c_str(),
		                     arrays.via, request.directed, LowerDrivingSide(request), request.details,
		                     request.strict, request.u_turn_on_edge, &call.route_rows, &call.count,
		                     &call.log, &call.notice, &call.err);
		return true;
	}
	default:
		return false;
	}
}

} // namespace duckdb_pgrouting
