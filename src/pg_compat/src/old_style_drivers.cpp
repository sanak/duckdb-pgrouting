// SPDX-License-Identifier: GPL-2.0-or-later

// Calls the pgRouting drivers that v4.0.2 has not moved onto the unified do_shortestPath. Their
// headers include postgres.h, so this translation unit belongs to the pg_compat side and includes
// no DuckDB header; exec_common.cpp reaches it through pgrouting/old_style_drivers.hpp only.
//
// One case per family. If upstream moves a family onto do_shortestPath, delete its case and switch
// that family's spec rows (shortest_path_specs.cpp) to DriverKind::SHORTEST_PATH.

#include "pgrouting/old_style_drivers.hpp"

#include "cpp_common/alloc.hpp"
#include "drivers/bdDijkstra/bdDijkstra_driver.h"
#include "drivers/bellman_ford/bellman_ford_driver.h"
#include "drivers/bellman_ford/edwardMoore_driver.h"
#include "drivers/breadthFirstSearch/binaryBreadthFirstSearch_driver.h"
#include "drivers/dagShortestPath/dagShortestPath_driver.h"

namespace duckdb_pgrouting {

namespace {

// The drivers allocate their messages with to_pg_msg (malloc here, see pg_types.cpp). Copy one and
// release the original, as upstream's pgr_global_report does after reporting it.
std::string TakeMessage(char *message) {
	if (!message) {
		return std::string();
	}
	std::string text(message);
	SPI_pfree(message);
	return text;
}

} // namespace

OldStyleOutput RunOldStyle(DriverKind kind, const std::string &edges_sql, const std::string &combinations_sql,
                           ArrayType *starts, ArrayType *ends, bool directed, bool only_cost, bool normal) {
	OldStyleOutput out;
	char *log = nullptr;
	char *notice = nullptr;
	char *err = nullptr;
	const char *edges = edges_sql.c_str();
	// Upstream's C entries pass NULL as the combinations query of an array form, and the drivers
	// branch on that pointer: a non-NULL one is fetched. "" must therefore never reach them.
	const char *combinations = combinations_sql.empty() ? nullptr : combinations_sql.c_str();
	try {
		switch (kind) {
		case DriverKind::BD_DIJKSTRA:
			pgr_do_bdDijkstra(edges, combinations, starts, ends, directed, only_cost, &out.rows, &out.count, &log,
			                  &notice, &err);
			break;
		case DriverKind::BELLMAN_FORD:
			pgr_do_bellman_ford(edges, combinations, starts, ends, directed, only_cost, &out.rows, &out.count, &log,
			                    &notice, &err);
			break;
		case DriverKind::EDWARD_MOORE:
			// No only_cost: pgr_edwardMoore has no Cost variant.
			pgr_do_edwardMoore(edges, combinations, starts, ends, directed, &out.rows, &out.count, &log, &notice,
			                   &err);
			break;
		case DriverKind::DAG_SHORTEST_PATH:
			// No directed: the driver always builds a directed graph. normal is passed through.
			pgr_do_dagShortestPath(edges, combinations, starts, ends, only_cost, normal, &out.rows, &out.count, &log,
			                       &notice, &err);
			break;
		case DriverKind::BINARY_BFS:
			// No only_cost: pgr_binaryBreadthFirstSearch has no Cost variant.
			pgr_do_binaryBreadthFirstSearch(edges, combinations, starts, ends, directed, &out.rows, &out.count, &log,
			                                &notice, &err);
			break;
		case DriverKind::SHORTEST_PATH:
			out.err = "Internal error: RunOldStyle called for the unified driver";
			return out;
		}
	} catch (const std::string &message) {
		// The drivers catch everything their body throws, but to_pg_msg can still throw
		// "Out of memory!" from inside one of those catch blocks. Catching it here relies on a
		// C++ exception crossing the drivers' extern "C" declarations, as pgr_compat_ereport
		// (pg_types.cpp) already does to report every other pgRouting error; MSVC's default
		// /EHsc assumes this cannot happen. The exposure is narrow: only to_pg_msg running out
		// of memory inside a driver's own catch block reaches this handler that way.
		out.err = message;
	}
	out.log = TakeMessage(log);
	out.notice = TakeMessage(notice);
	if (out.err.empty()) {
		out.err = TakeMessage(err);
	} else {
		TakeMessage(err);
	}
	return out;
}

} // namespace duckdb_pgrouting
