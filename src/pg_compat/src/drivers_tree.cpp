// SPDX-License-Identifier: GPL-2.0-or-later

// The per-family drivers whose rows are trees (ResultShape::MST): every vertex reached from one or
// more roots, with its predecessor, depth and cost. Each case passes exactly the arguments its
// upstream C entry (src/<family>/*.c) passes.

#include <cctype>

#include "driver_groups.hpp"

#include "drivers/breadthFirstSearch/breadthFirstSearch_driver.h"
#include "drivers/driving_distance/driving_distance_driver.h"
#include "drivers/driving_distance/driving_distance_withPoints_driver.h"
#include "drivers/spanningTree/kruskal_driver.h"
#include "drivers/spanningTree/prim_driver.h"
#include "drivers/traversal/depthFirstSearch_driver.h"
#include "pgrouting/withpoints_keys.hpp"

namespace duckdb_pgrouting {

bool CallTreeDriver(const DriverRequest &request, const DriverArrays &arrays, DriverCall &call) {
	const char *edges = request.edges_sql.c_str();
	switch (request.driver) {
	case DriverKind::DRIVING_DISTANCE:
		pgr_do_drivingDistance(edges, arrays.roots, request.distance, request.directed, request.equicost,
		                       &call.mst_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	case DriverKind::WITH_POINTS_DD: {
		// The C entry lowercases the side (estimate_drivingSide; _pgr_exec has already rejected
		// anything but r, l, b) and derives the two edge queries with get_new_queries
		// (src/withPoints/get_new_queries.cpp), whose template WithPointsDerivedKeys reproduces:
		// those strings are the keys the derived inputs are registered under.
		const auto keys = WithPointsDerivedKeys(request.edges_sql, request.points_sql);
		const auto side = static_cast<char>(std::tolower(static_cast<unsigned char>(request.driving_side)));
		pgr_do_withPointsDD(keys.no_points.c_str(), request.points_sql.c_str(), keys.of_points.c_str(),
		                    arrays.roots, request.distance, side, request.directed, request.details,
		                    request.equicost, &call.mst_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	}
	case DriverKind::KRUSKAL:
		pgr_do_kruskal(edges, arrays.roots, request.mst_suffix.c_str(), request.max_depth, request.distance,
		               &call.mst_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	case DriverKind::PRIM:
		pgr_do_prim(edges, arrays.roots, request.mst_suffix.c_str(), request.max_depth, request.distance,
		            &call.mst_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	case DriverKind::BREADTH_FIRST_SEARCH:
		pgr_do_breadthFirstSearch(edges, arrays.roots, request.max_depth, request.directed, &call.mst_rows,
		                          &call.count, &call.log, &call.notice, &call.err);
		return true;
	case DriverKind::DEPTH_FIRST_SEARCH:
		// directed before max_depth, the reverse of breadth-first search's order.
		pgr_do_depthFirstSearch(edges, arrays.roots, request.directed, request.max_depth, &call.mst_rows,
		                        &call.count, &call.log, &call.notice, &call.err);
		return true;
	default:
		return false;
	}
}

} // namespace duckdb_pgrouting
