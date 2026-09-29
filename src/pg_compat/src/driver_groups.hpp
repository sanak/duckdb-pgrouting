// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Shared by family_drivers.cpp and the drivers_*.cpp files, all on the pg_compat side. Each
// drivers_*.cpp calls one group of upstream's per-family drivers, so no single file includes every
// driver header; family_drivers.cpp asks each group in turn.

#include <cctype>
#include <cstddef>

#include "pgrouting/family_drivers.hpp"

struct II_t_rt;
struct IID_t_rt;
struct MST_rt;
struct Path_rt;
struct Routes_t;
struct TSP_tour_rt;

namespace duckdb_pgrouting {

// A driver's raw outputs. One row pointer per upstream row struct, so every driver writes through a
// pointer of its own row type; RunFamilyDriver hands on the one its shape names (II_t_rt rows serve
// PAIRS and ID_VALUE, Path_rt rows PATH and KSP).
struct DriverCall {
	Path_rt *path_rows = nullptr;
	II_t_rt *pair_rows = nullptr;
	MST_rt *mst_rows = nullptr;
	Routes_t *route_rows = nullptr;
	TSP_tour_rt *tour_rows = nullptr;
	int64_t *id_rows = nullptr;
	IID_t_rt *triple_rows = nullptr;
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

// The withPoints C entries pass estimate_drivingSide's lowercased side; _pgr_exec has already
// rejected anything but r, l and b.
inline char LowerDrivingSide(const DriverRequest &request) {
	return static_cast<char>(std::tolower(static_cast<unsigned char>(request.driving_side)));
}

// The edge query's rows counted as base_graph.hpp's graph_add_edge inserts them: a row enters the
// graph when its cost or its reverse_cost is non-negative. `readable` is false when a column the
// count needs (id, source, target, cost, and reverse_cost if present) is missing, not of upstream's
// type class, or NULL in some row; the driver then reports that itself. The crash guards read it.
struct EdgeCensus {
	bool readable = false;
	std::size_t rows = 0;
	std::size_t inserted = 0;
	std::size_t self_loops = 0; // inserted rows whose source is their target
};
EdgeCensus CountEdges(const DriverRequest &request);

// Each calls request.driver if it belongs to the group and returns false otherwise.
bool CallPathDriver(const DriverRequest &request, const DriverArrays &arrays, DriverCall &call);
bool CallGraphDriver(const DriverRequest &request, const DriverArrays &arrays, DriverCall &call);
bool CallTreeDriver(const DriverRequest &request, const DriverArrays &arrays, DriverCall &call);
bool CallRoutesDriver(const DriverRequest &request, const DriverArrays &arrays, DriverCall &call);
bool CallTrspDriver(const DriverRequest &request, const DriverArrays &arrays, DriverCall &call);
bool CallTspDriver(const DriverRequest &request, const DriverArrays &arrays, DriverCall &call);
bool CallUnifiedDriver(const DriverRequest &request, const DriverArrays &arrays, DriverCall &call);

} // namespace duckdb_pgrouting
