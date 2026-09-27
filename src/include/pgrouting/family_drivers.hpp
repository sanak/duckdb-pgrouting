// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// The seam to pgRouting's per-family drivers. Their own headers include postgres.h, so they are
// called from the pg_compat side (src/pg_compat/src/family_drivers.cpp and the drivers_*.cpp files
// it dispatches to); this header includes neither DuckDB nor the compat postgres.h, like
// input_access.hpp.

#include <cstddef>
#include <string>

#include "pgrouting/driver_request.hpp"

struct ArrayType;

namespace duckdb_pgrouting {

// The PostgreSQL arrays a driver takes, built by the caller from the request's id lists. nullptr
// means "argument not given".
struct DriverArrays {
	ArrayType *starts = nullptr;
	ArrayType *ends = nullptr;
};

struct DriverOutput {
	ResultShape shape = ResultShape::PATH; // what `rows` points to: InfoOf(request.driver).shape
	void *rows = nullptr;                  // allocated by the driver with malloc; the caller owns it
	std::size_t count = 0;
	std::string log;
	std::string notice;
	std::string err;
};

// Runs one per-family driver. request.driver must not be SHORTEST_PATH. An empty combinations_sql
// means "array form".
DriverOutput RunFamilyDriver(const DriverRequest &request, const DriverArrays &arrays);

} // namespace duckdb_pgrouting
