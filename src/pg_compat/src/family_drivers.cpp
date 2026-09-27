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
		return call.path_rows;
	case ResultShape::PAIRS:
		return call.pair_rows;
	}
	return nullptr;
}

// The field a driver of this shape should never have written: path_rows for a PAIRS driver, or
// pair_rows for a PATH one. A driver that writes there anyway has the wrong shape for its
// DriverKind, which InfoOf() -- not the driver's own choice -- declares.
void *WrongShapeRows(const DriverCall &call, ResultShape shape) {
	switch (shape) {
	case ResultShape::PATH:
		return call.pair_rows;
	case ResultShape::PAIRS:
		return call.path_rows;
	}
	return nullptr;
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
		if (!CallPathDriver(request, arrays, call) && !CallGraphDriver(request, arrays, call)) {
			out.err = std::string("Internal error: no family driver for '") + InfoOf(request.driver).name + "'";
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
	out.rows = RowsOf(call, shape);
	out.count = call.count;
	// A meaningful shape check, unlike comparing InfoOf(request.driver).shape against itself: a
	// driver that populated the other DriverCall field, or reported rows without populating either,
	// wrote something RunDriver's caller cannot safely interpret as this shape.
	if (out.err.empty()) {
		if (void *wrong_rows = WrongShapeRows(call, shape)) {
			std::free(wrong_rows);
			out.err = WrongShapeErr(InfoOf(request.driver).name);
		} else if (call.count > 0 && out.rows == nullptr) {
			out.err = WrongShapeErr(InfoOf(request.driver).name);
		}
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
