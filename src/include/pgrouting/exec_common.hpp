// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "pgrouting/driver_kind.hpp"

struct Path_rt;

namespace duckdb {
class ClientContext;
}

namespace duckdb_pgrouting {

class InputRegistry;

// Everything a pgRouting driver needs, in this extension's own vocabulary. The exec table function
// fills it; the public overloads decide the fixed flags.
struct DriverRequest {
	std::string edges_sql;
	std::string points_sql;
	std::string combinations_sql;
	std::vector<int64_t> starts;
	std::vector<int64_t> ends;
	bool has_starts = false;
	bool has_ends = false;
	bool directed = true;
	bool only_cost = false;
	bool normal = true;
	int64_t n_goals = 0;
	bool global = false;
	char driving_side = ' ';
	bool details = true;
	int32_t which = 0;
	// Which driver runs the request; only SHORTEST_PATH reads points_sql, n_goals, global,
	// driving_side, details and which.
	DriverKind driver = DriverKind::SHORTEST_PATH;
};

// Owns the driver's malloc'd tuple array and frees it.
class DriverResult {
public:
	DriverResult() = default;
	~DriverResult();
	DriverResult(DriverResult &&other) noexcept;
	DriverResult &operator=(DriverResult &&other) noexcept;
	DriverResult(const DriverResult &) = delete;
	DriverResult &operator=(const DriverResult &) = delete;

	Path_rt *rows = nullptr;
	std::size_t count = 0;
	bool is_matrix = false;
};

// Runs the driver with the registry active, maps its messages and exceptions to DuckDB ones.
DriverResult RunShortestPath(duckdb::ClientContext &context, InputRegistry &registry, const DriverRequest &request);

} // namespace duckdb_pgrouting
