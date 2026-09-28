// SPDX-License-Identifier: GPL-2.0-or-later

// The turn-restriction drivers: pgr_trsp's and pgr_trsp_withPoints's (Path_rt, ResultShape::PATH),
// pgr_trspVia's and pgr_trspVia_withPoints's (Routes_t, ResultShape::ROUTES) and
// pgr_turnRestrictedPath's (Path_rt, ResultShape::KSP). Every one reads the restrictions query
// (cost, path) besides its edges. Each case passes exactly the arguments its upstream C entry
// (src/trsp/*.c, src/ksp/turnRestrictedPath.c) passes.

#include "driver_groups.hpp"

#include <cstddef>
#include <string>

#include "drivers/trsp/trsp_driver.h"
#include "drivers/trsp/trsp_withPoints_driver.h"
#include "drivers/trsp/trspVia_driver.h"
#include "drivers/trsp/trspVia_withPoints_driver.h"
#include "drivers/yen/turnRestrictedPath_driver.h"
#include "pgrouting/input_access.hpp"
#include "pgrouting/withpoints_keys.hpp"

namespace duckdb_pgrouting {

namespace {

// pgr_do_turnRestrictedPath builds a Rule from every restriction row, and Rule's constructor
// (src/cpp_common/rule.cpp) takes the path's last element: a NULL or empty path, which
// getBigIntArr reads as no array, is undefined behaviour there. The other turn-restriction drivers
// skip such a row (`if (r.via)`); this one is refused before the driver runs, even where the
// driver would have stopped earlier on an empty edge set. A missing or wrong-typed path column is
// left to the driver's own column check.
void RefuseEmptyRestrictionPaths(const DriverRequest &request) {
	const auto &input = LookupInput(request.restrictions_sql, KIND_RESTRICTIONS);
	const int path = FindColumn(input, "path");
	if (path == -1 || ColumnClassOf(input, path) != ColumnClass::INTEGER_ARRAY) {
		return;
	}
	for (std::size_t row = 0; row < InputRowCount(input); row++) {
		if (IsNull(input, row, path) || ReadInt64Array(input, row, path).empty()) {
			throw std::string("Unexpected NULL or empty array in column path");
		}
	}
}

} // namespace

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
	case DriverKind::TURN_RESTRICTED_PATH:
		// _pgr_turnRestrictedPath_v4 passes the id arrays and no combinations query. A negative K never
		// gets here (CheckRequest raises upstream's error), so the cast is safe.
		RefuseEmptyRestrictionPaths(request);
		pgr_do_turnRestrictedPath(edges, restrictions, nullptr, arrays.starts, arrays.ends,
		                          static_cast<size_t>(request.k), request.directed, request.heap_paths,
		                          request.stop_on_first, request.strict, &call.path_rows, &call.count,
		                          &call.log, &call.notice, &call.err);
		return true;
	default:
		return false;
	}
}

} // namespace duckdb_pgrouting
