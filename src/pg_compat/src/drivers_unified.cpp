// SPDX-License-Identifier: GPL-2.0-or-later

// Upstream's unified C++ drivers, each serving several public functions: do_ordering
// (src/ordering/ordering_driver.cpp) and do_allpairs (src/allpairs/allpairs_driver.cpp). Unlike the
// per-family drivers they report through std::ostringstream; their process files
// (ordering_process.cpp, allpairs_process.cpp) hand the streams to report_messages, and here they
// become DriverCall's malloc'd messages instead. Each case passes the Which value its C entry
// (src/ordering/*.c, src/allpairs/*.c) passes.

#include "driver_groups.hpp"

#include <sstream>

#include "cpp_common/alloc.hpp"
#include "drivers/allpairs_driver.hpp"
#include "drivers/ordering_driver.hpp"

namespace duckdb_pgrouting {

bool CallUnifiedDriver(const DriverRequest &request, const DriverArrays &, DriverCall &call) {
	std::ostringstream log;
	std::ostringstream notice;
	std::ostringstream err;
	switch (request.driver) {
	case DriverKind::CUTHILL_MCKEE_ORDERING:
		do_ordering(request.edges_sql, CUTCHILL, call.id_rows, call.count, log, notice, err);
		break;
	case DriverKind::KING_ORDERING:
		do_ordering(request.edges_sql, KING, call.id_rows, call.count, log, notice, err);
		break;
	case DriverKind::SLOAN_ORDERING: {
		// Boost's sloan_ordering (src/ordering/sloanOrdering.cpp) crashes the process on a graph
		// whose vertices have no edge between them -- an edge query whose every row has both costs
		// negative, since do_ordering still extracts those rows' vertices. Nothing is ordered; the
		// driver is not called. Boost sizes a vector by the graph's maximum degree and indexes it by
		// degree; with no inserted edge that vector is empty.
		const auto census = CountEdges(request);
		if (census.readable && census.rows > 0 && census.inserted == 0) {
			return true;
		}
		do_ordering(request.edges_sql, SLOAN, call.id_rows, call.count, log, notice, err);
		break;
	}
	case DriverKind::TOPOLOGICAL_SORT:
		do_ordering(request.edges_sql, TOPOSORT, call.id_rows, call.count, log, notice, err);
		break;
	case DriverKind::JOHNSON:
		// allpairs_process.cpp: which = 0 is Johnson, 1 is Floyd-Warshall. No notice stream.
		do_allpairs(request.edges_sql, request.directed, 0, call.triple_rows, call.count, log, err);
		break;
	case DriverKind::FLOYD_WARSHALL:
		do_allpairs(request.edges_sql, request.directed, 1, call.triple_rows, call.count, log, err);
		break;
	default:
		return false;
	}
	call.log = pgrouting::to_pg_msg(log);
	call.notice = pgrouting::to_pg_msg(notice);
	call.err = pgrouting::to_pg_msg(err);
	return true;
}

} // namespace duckdb_pgrouting
