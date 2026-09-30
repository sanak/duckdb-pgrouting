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
	ArrayType *roots = nullptr;
	ArrayType *via = nullptr;
	ArrayType *methods = nullptr;
	ArrayType *forbidden = nullptr;
};

struct DriverOutput {
	// Allocated by the driver with malloc; the caller owns it. Shaped as InfoOf(request.driver).shape
	// declares -- RunFamilyDriver itself checks that against what the driver actually wrote and
	// reports a mismatch through err, so nothing further down needs to re-check it.
	void *rows = nullptr;
	std::size_t count = 0;
	std::string log;
	std::string notice;
	std::string err;
};

// Runs one per-family driver. request.driver must not be SHORTEST_PATH. An empty combinations_sql
// means "array form". err is set if the driver's own per-family case does not exist, or if it wrote
// rows that do not match InfoOf(request.driver).shape; the caller (RunDriver) drops such a result.
DriverOutput RunFamilyDriver(const DriverRequest &request, const DriverArrays &arrays);

} // namespace duckdb_pgrouting
