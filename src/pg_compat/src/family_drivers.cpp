// SPDX-License-Identifier: GPL-2.0-or-later

// Runs one of the pgRouting drivers that v4.0.2 has not moved onto the unified do_shortestPath.
// This file and the drivers_*.cpp files it dispatches to belong to the pg_compat side and include
// no DuckDB header; exec_common.cpp reaches them through pgrouting/family_drivers.hpp only.
//
// If upstream moves a family onto do_shortestPath, delete its case from its drivers_*.cpp and
// switch that family's spec rows to DriverKind::SHORTEST_PATH.

#include "pgrouting/family_drivers.hpp"

#include <cstdlib>

#include "cpp_common/alloc.hpp"
#include "driver_groups.hpp"

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

void *RowsOf(const DriverCall &call, ResultShape shape) {
	switch (shape) {
	case ResultShape::PATH:
	case ResultShape::KSP:
		return call.path_rows;
	case ResultShape::PAIRS:
	case ResultShape::ID_VALUE:
		return call.pair_rows;
	case ResultShape::MST:
		return call.mst_rows;
	case ResultShape::ROUTES:
		return call.route_rows;
	case ResultShape::TSP_TOUR:
		return call.tour_rows;
	case ResultShape::IDS:
		return call.id_rows;
	case ResultShape::TRIPLES:
		return call.triple_rows;
	case ResultShape::EDGE:
		return call.edge_rows;
	case ResultShape::LINE_GRAPH_FULL:
		return call.line_graph_full_rows;
	case ResultShape::TRANSITIVE_CLOSURE:
		return call.closure_rows;
	case ResultShape::STOER_WAGNER:
		return call.min_cut_rows;
	case ResultShape::CIRCUITS:
		return call.circuit_rows;
	}
	return nullptr;
}

// Frees every row pointer a driver of this shape should never have written, and reports whether
// there was one. A driver that writes one anyway has the wrong shape for its DriverKind, which
// InfoOf() -- not the driver's own choice -- declares. Path_rt rows belong to two shapes, and so do
// II_t_rt rows.
bool DropWrongShapeRows(DriverCall &call, ResultShape shape) {
	bool wrong = false;
	auto drop = [&](auto *&rows, bool own) {
		if (!own && rows != nullptr) {
			std::free(rows);
			rows = nullptr;
			wrong = true;
		}
	};
	drop(call.path_rows, shape == ResultShape::PATH || shape == ResultShape::KSP);
	drop(call.pair_rows, shape == ResultShape::PAIRS || shape == ResultShape::ID_VALUE);
	drop(call.mst_rows, shape == ResultShape::MST);
	drop(call.route_rows, shape == ResultShape::ROUTES);
	drop(call.tour_rows, shape == ResultShape::TSP_TOUR);
	drop(call.id_rows, shape == ResultShape::IDS);
	drop(call.triple_rows, shape == ResultShape::TRIPLES);
	drop(call.edge_rows, shape == ResultShape::EDGE);
	drop(call.line_graph_full_rows, shape == ResultShape::LINE_GRAPH_FULL);
	// Frees only the outer block: a driver that writes rows of the wrong shape is an internal error
	// already, and its arrays are not ours to walk.
	drop(call.closure_rows, shape == ResultShape::TRANSITIVE_CLOSURE);
	drop(call.min_cut_rows, shape == ResultShape::STOER_WAGNER);
	drop(call.circuit_rows, shape == ResultShape::CIRCUITS);
	return wrong;
}

std::string WrongShapeErr(const char *driver_name) {
	return std::string("Internal error: driver '") + driver_name + "' wrote rows of the wrong shape";
}

} // namespace

DriverOutput RunFamilyDriver(const DriverRequest &request, const DriverArrays &arrays) {
	DriverOutput out;
	auto shape = InfoOf(request.driver).shape;
	DriverCall call;
	try {
		if (!CallPathDriver(request, arrays, call) && !CallGraphDriver(request, arrays, call) &&
		    !CallTreeDriver(request, arrays, call) && !CallRoutesDriver(request, arrays, call) &&
		    !CallTrspDriver(request, arrays, call) && !CallTspDriver(request, arrays, call) &&
		    !CallUnifiedDriver(request, arrays, call) &&
		    !CallAnalysisDriver(request, arrays, call)) {
			out.err = std::string("Internal error: no family driver for '") + InfoOf(request.driver).name + "'";
		}
	} catch (const std::string &message) {
		// The adapters' own input checks (drivers_trsp.cpp) and the registry lookups they make throw
		// std::string before any driver runs. The drivers catch everything their body throws, but
		// to_pg_msg can still throw "Out of memory!" from inside one of those catch blocks. Catching
		// it here relies on a C++ exception crossing the drivers' extern "C" declarations, as
		// pgr_compat_ereport (pg_types.cpp) already does to report every other pgRouting error; MSVC's
		// default /EHsc assumes this cannot happen. The exposure is narrow: only to_pg_msg running out
		// of memory inside a driver's own catch block reaches this handler that way.
		out.err = message;
	}
	out.rows = RowsOf(call, shape);
	out.count = call.count;
	// A meaningful shape check, unlike comparing InfoOf(request.driver).shape against itself: a
	// driver that populated another DriverCall field, or reported rows without populating its own,
	// wrote something RunDriver's caller cannot safely interpret as this shape. The stray rows are
	// freed whatever else went wrong.
	const bool wrong_shape = DropWrongShapeRows(call, shape) || (call.count > 0 && out.rows == nullptr);
	if (out.err.empty() && wrong_shape) {
		out.err = WrongShapeErr(InfoOf(request.driver).name);
	}
	out.log = TakeMessage(call.log);
	out.notice = TakeMessage(call.notice);
	if (out.err.empty()) {
		out.err = TakeMessage(call.err);
	} else {
		TakeMessage(call.err);
	}
	return out;
}

} // namespace duckdb_pgrouting
