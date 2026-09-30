// SPDX-License-Identifier: GPL-2.0-or-later

// The contraction drivers: pgr_contraction, pgr_contractionDeadEnd and pgr_contractionLinear (one
// driver, told which contractions to run) and pgr_contractionHierarchies. Each case passes exactly
// the arguments its upstream C entry (src/contraction/*.c) passes.

#include "driver_groups.hpp"

#include "drivers/contraction/contractGraph_driver.h"
#include "drivers/contraction/contractionHierarchies_driver.h"

namespace duckdb_pgrouting {

bool CallContractionDriver(const DriverRequest &request, const DriverArrays &arrays, DriverCall &call) {
	const char *edges = request.edges_sql.c_str();
	switch (request.driver) {
	case DriverKind::CONTRACTION:
		// src/contraction/contractGraph.c passes methods as the contraction order and cycles as its
		// num_cycles; RequestCheck::CONTRACTION_CYCLES already answered cycles < 1.
		pgr_do_contractGraph(edges, arrays.forbidden, arrays.methods, request.cycles, request.directed,
		                     &call.contracted_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	case DriverKind::CONTRACTION_HIERARCHIES:
		// src/contraction/contractionHierarchies.c: no methods and no cycles.
		pgr_contractionHierarchies(edges, arrays.forbidden, request.directed, &call.hierarchy_rows, &call.count,
		                           &call.log, &call.notice, &call.err);
		return true;
	default:
		return false;
	}
}

} // namespace duckdb_pgrouting
