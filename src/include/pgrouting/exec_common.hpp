// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>

#include "pgrouting/driver_request.hpp"

namespace duckdb {
class ClientContext;
}

namespace duckdb_pgrouting {

class InputRegistry;

// Owns the driver's malloc'd rows and frees them.
class DriverResult {
public:
	DriverResult() = default;
	~DriverResult();
	DriverResult(DriverResult &&other) noexcept;
	DriverResult &operator=(DriverResult &&other) noexcept;
	DriverResult(const DriverResult &) = delete;
	DriverResult &operator=(const DriverResult &) = delete;

	// The rows as the upstream struct `shape` names: Path_rt for PATH and KSP, MST_rt for MST,
	// Routes_t for ROUTES, TSP_tour_rt for TSP_TOUR, int64_t for IDS, II_t_rt for PAIRS and
	// ID_VALUE, IID_t_rt for TRIPLES, Edge_rt for EDGE,
	// Line_graph_full_rt for LINE_GRAPH_FULL, TransitiveClosure_rt for TRANSITIVE_CLOSURE,
	// StoerWagner_t for STOER_WAGNER.
	template <class T>
	const T *Rows() const {
		return static_cast<const T *>(rows);
	}

	ResultShape shape = ResultShape::PATH;
	void *rows = nullptr;
	std::size_t count = 0;
	bool is_matrix = false;

private:
	void Release();
};

// Runs the request's driver with the registry active, and maps its messages and exceptions to
// DuckDB ones.
DriverResult RunDriver(duckdb::ClientContext &context, InputRegistry &registry, const DriverRequest &request);

} // namespace duckdb_pgrouting
