// SPDX-License-Identifier: GPL-2.0-or-later

// The per-family drivers whose rows are not paths: components, connectivity and coloring. Each
// case passes exactly the arguments its upstream C entry (src/components/*.c, src/coloring/*.c)
// passes.

#include "driver_groups.hpp"

#include <initializer_list>

#include "drivers/coloring/bipartite_driver.h"
#include "drivers/coloring/edgeColoring_driver.h"
#include "drivers/coloring/sequentialVertexColoring_driver.h"
#include "drivers/components/articulationPoints_driver.h"
#include "drivers/components/biconnectedComponents_driver.h"
#include "drivers/components/bridges_driver.h"
#include "drivers/components/connectedComponents_driver.h"
#include "drivers/components/makeConnected_driver.h"
#include "drivers/components/strongComponents_driver.h"
#include "pgrouting/input_access.hpp"

namespace duckdb_pgrouting {

EdgeCensus CountEdges(const DriverRequest &request) {
	const auto &input = LookupInput(request.edges_sql, KIND_EDGES);
	const int id = FindColumn(input, "id");
	const int source = FindColumn(input, "source");
	const int target = FindColumn(input, "target");
	const int cost = FindColumn(input, "cost");
	const int reverse_cost = FindColumn(input, "reverse_cost");
	const auto integer = [&](int column) {
		return column != -1 && ColumnClassOf(input, column) == ColumnClass::INTEGER;
	};
	const auto number = [&](int column) {
		return column != -1 && (ColumnClassOf(input, column) == ColumnClass::INTEGER ||
		                        ColumnClassOf(input, column) == ColumnClass::NUMERIC);
	};
	EdgeCensus census;
	if (!integer(id) || !integer(source) || !integer(target) || !number(cost) ||
	    (reverse_cost != -1 && !number(reverse_cost))) {
		return census;
	}
	census.rows = InputRowCount(input);
	for (std::size_t row = 0; row < census.rows; row++) {
		for (const int column : {id, source, target, cost, reverse_cost}) {
			if (column != -1 && IsNull(input, row, column)) {
				return EdgeCensus();
			}
		}
		const bool inserted =
		    ReadDouble(input, row, cost) >= 0 || (reverse_cost != -1 && ReadDouble(input, row, reverse_cost) >= 0);
		if (!inserted) {
			continue;
		}
		census.inserted++;
		if (ReadInt64(input, row, source) == ReadInt64(input, row, target)) {
			census.self_loops++;
		}
	}
	census.readable = true;
	return census;
}

bool CallGraphDriver(const DriverRequest &request, const DriverArrays &, DriverCall &call) {
	const char *edges = request.edges_sql.c_str();
	switch (request.driver) {
	case DriverKind::CONNECTED_COMPONENTS:
		// Upstream's develop branch replaces this driver with do_coloring(..., CONNECTEDCOMPONENTS);
		// at the pgRouting bump that brings it, call that here instead.
		pgr_do_connectedComponents(edges, &call.pair_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	case DriverKind::STRONG_COMPONENTS:
		pgr_do_strongComponents(edges, &call.pair_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	case DriverKind::BICONNECTED_COMPONENTS: {
		// Boost's biconnected_components puts a self-loop in no component, and upstream's
		// biconnectedComponents (src/components/components.cpp) files every edge under the index an
		// associative map reads back -- 0 for a self-loop -- in a vector sized by the component
		// count. A graph whose every inserted edge is a self-loop has no component, so that is
		// element 0 of an empty vector: undefined behaviour that crashes the process (PostgreSQL's
		// backend too). Such a graph has no biconnected component; the driver is not called.
		const auto census = CountEdges(request);
		if (census.readable && census.inserted > 0 && census.self_loops == census.inserted) {
			return true;
		}
		pgr_do_biconnectedComponents(edges, &call.pair_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	}
	case DriverKind::ARTICULATION_POINTS:
		pgr_do_articulationPoints(edges, &call.id_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	case DriverKind::BRIDGES:
		pgr_do_bridges(edges, &call.id_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	case DriverKind::MAKE_CONNECTED:
		pgr_do_makeConnected(edges, &call.pair_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	case DriverKind::SEQUENTIAL_VERTEX_COLORING:
		pgr_do_sequentialVertexColoring(edges, &call.pair_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	case DriverKind::BIPARTITE:
		pgr_do_bipartite(edges, &call.pair_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	case DriverKind::EDGE_COLORING:
		pgr_do_edgeColoring(edges, &call.pair_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	default:
		return false;
	}
}

} // namespace duckdb_pgrouting
