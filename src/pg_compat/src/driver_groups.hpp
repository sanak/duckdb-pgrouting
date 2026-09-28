// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Shared by family_drivers.cpp and the drivers_*.cpp files, all on the pg_compat side. Each
// drivers_*.cpp calls one group of upstream's per-family drivers, so no single file includes every
// driver header; family_drivers.cpp asks each group in turn.

#include <cstddef>

#include "pgrouting/family_drivers.hpp"

struct II_t_rt;
struct MST_rt;
struct Path_rt;

namespace duckdb_pgrouting {

// A driver's raw outputs. One row pointer per upstream row struct, so every driver writes through a
// pointer of its own row type; RunFamilyDriver hands on the one its shape names.
struct DriverCall {
	Path_rt *path_rows = nullptr;
	II_t_rt *pair_rows = nullptr;
	MST_rt *mst_rows = nullptr;
	std::size_t count = 0;
	char *log = nullptr;
	char *notice = nullptr;
	char *err = nullptr;
};

// Upstream's C entries pass NULL as the combinations query of an array form, and the drivers
// branch on that pointer: "" must therefore never reach them.
inline const char *CombinationsOrNull(const DriverRequest &request) {
	return request.combinations_sql.empty() ? nullptr : request.combinations_sql.c_str();
}

// Each calls request.driver if it belongs to the group and returns false otherwise.
bool CallPathDriver(const DriverRequest &request, const DriverArrays &arrays, DriverCall &call);
bool CallGraphDriver(const DriverRequest &request, const DriverArrays &arrays, DriverCall &call);
bool CallTreeDriver(const DriverRequest &request, const DriverArrays &arrays, DriverCall &call);
bool CallRoutesDriver(const DriverRequest &request, const DriverArrays &arrays, DriverCall &call);

} // namespace duckdb_pgrouting
