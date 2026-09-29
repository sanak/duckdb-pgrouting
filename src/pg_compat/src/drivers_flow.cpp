// SPDX-License-Identifier: GPL-2.0-or-later

// The flow drivers: maximum flow (pgr_pushRelabel, pgr_boykovKolmogorov, pgr_edmondsKarp,
// pgr_maxFlow), minimum-cost maximum flow, edge-disjoint paths, maximum cardinality matching and
// the Chinese Postman. Each case passes exactly the arguments its upstream C entry
// (src/max_flow/*.c, src/chinese/chinesePostman.c) passes.

#include "driver_groups.hpp"

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <unordered_map>
#include <unordered_set>

#include "drivers/chinese/chinesePostman_driver.h"
#include "drivers/max_flow/edge_disjoint_paths_driver.h"
#include "drivers/max_flow/max_flow_driver.h"
#include "drivers/max_flow/maximum_cardinality_matching_driver.h"
#include "drivers/max_flow/minCostMaxFlow_driver.h"
#include "pgrouting/input_access.hpp"

namespace duckdb_pgrouting {

namespace {

// pgr_chinesePostman's tour starts at the source of the edge query's first row (PgrDirectedChPPGraph's
// constructor, include/chinese/chinesePostman.hpp), and its graph holds only the directions with a
// positive cost. Upstream crashes when that vertex lies on no positive direction and either nothing
// has a positive direction (it reads the last key of an empty map) or the positive directions are one
// connected piece (it walks the whole piece from another vertex and then looks up an edge out of the
// start, a null edge its lookup map default-constructs). When they are several pieces upstream returns
// no tour but still the cost, so the driver is called. The crashing cases are answered here with no
// rows, as the driver's no-tour answer, without calling the driver. Input the driver would reject - a
// missing or mistyped column, a NULL - is left to it; so is an empty query, which it reports.
bool PostmanStartsOffTheGraph(const DriverRequest &request) {
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
	if (!integer(id) || !integer(source) || !integer(target) || !number(cost) ||
	    (reverse_cost != -1 && !number(reverse_cost))) {
		return false;
	}
	const auto rows = InputRowCount(input);
	if (rows == 0) {
		return false;
	}
	for (std::size_t row = 0; row < rows; row++) {
		for (const int column : {id, source, target, cost, reverse_cost}) {
			if (column != -1 && IsNull(input, row, column)) {
				return false;
			}
		}
	}
	const int64_t start = ReadInt64(input, 0, source);
	// Union-find over the vertices of the positive directions (direction is irrelevant to connectivity).
	std::unordered_map<int64_t, int64_t> parent;
	const std::function<int64_t(int64_t)> find = [&](int64_t v) {
		while (parent[v] != v) {
			parent[v] = parent[parent[v]];
			v = parent[v];
		}
		return v;
	};
	for (std::size_t row = 0; row < rows; row++) {
		const bool positive =
		    ReadDouble(input, row, cost) > 0 || (reverse_cost != -1 && ReadDouble(input, row, reverse_cost) > 0);
		if (!positive) {
			continue;
		}
		const int64_t from = ReadInt64(input, row, source);
		const int64_t to = ReadInt64(input, row, target);
		if (from == start || to == start) {
			return false;
		}
		parent.emplace(from, from);
		parent.emplace(to, to);
		parent[find(from)] = find(to);
	}
	if (parent.empty()) {
		return true;
	}
	std::unordered_set<int64_t> roots;
	for (const auto &entry : parent) {
		roots.insert(find(entry.first));
	}
	return roots.size() == 1;
}

} // namespace

bool CallFlowDriver(const DriverRequest &request, const DriverArrays &arrays, DriverCall &call) {
	const char *edges = request.edges_sql.c_str();
	const char *combinations = CombinationsOrNull(request);
	switch (request.driver) {
	case DriverKind::MAX_FLOW:
		// only_cost stands for upstream's only_flow: pgr_maxFlow's single row carries the total.
		pgr_do_max_flow(edges, combinations, arrays.starts, arrays.ends, request.algorithm, request.only_cost,
		                &call.flow_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	case DriverKind::MIN_COST_MAX_FLOW:
		pgr_do_minCostMaxFlow(edges, combinations, arrays.starts, arrays.ends, request.only_cost, &call.flow_rows,
		                      &call.count, &call.log, &call.notice, &call.err);
		return true;
	case DriverKind::EDGE_DISJOINT_PATHS:
		// Each path ends with a row of edge -1. src/max_flow/edge_disjoint_paths.c numbers the rows as
		// the K-shortest-path C entries do (EmitKsp), except that it restarts path_seq only after edge
		// -1, which differs only for negative edge ids of the caller's own.
		pgr_do_edge_disjoint_paths(edges, combinations, arrays.starts, arrays.ends, request.directed,
		                           &call.path_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	case DriverKind::MAX_CARDINALITY_MATCH:
		// v4.0 has no directed: the graph is undirected, and the rows are the matched edge ids, sorted.
		pgr_do_maximum_cardinality_matching(edges, &call.id_rows, &call.count, &call.log, &call.notice, &call.err);
		return true;
	case DriverKind::CHINESE_POSTMAN:
		if (PostmanStartsOffTheGraph(request)) {
			return true;
		}
		// Always a directed graph: src/chinese/chinesePostman.c passes only only_cost.
		pgr_do_directedChPP(edges, request.only_cost, &call.path_rows, &call.count, &call.log, &call.notice,
		                    &call.err);
		return true;
	default:
		return false;
	}
}

} // namespace duckdb_pgrouting
