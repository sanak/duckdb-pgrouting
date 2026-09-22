// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Every public overload declared as data. The registered name is the upstream name, verbatim and
// with upstream's casing, so a spec row can be compared with pgRouting's own SQL signature files
// character by character.

#include <cstdint>
#include <string>

#include "duckdb.hpp"

#include "pgrouting/driver_kind.hpp"

namespace duckdb_pgrouting {

// What a required positional argument of a public function means.
enum class ArgKind : uint8_t {
	EDGES_SQL,        // VARCHAR
	COMBINATIONS_SQL, // VARCHAR
	START_VID,        // BIGINT
	END_VID,          // BIGINT
	START_VIDS,       // BIGINT[]
	END_VIDS,         // BIGINT[]
	POINTS_SQL,       // VARCHAR
	DRIVING_SIDE,     // VARCHAR; upstream's CHAR, of which only the first character is used
	VIDS              // BIGINT[] passed as both the starts and the ends, as pgr_bdDijkstraCostMatrix does
};

enum class OptionalType : uint8_t { BOOLEAN, BIGINT };

// A parameter upstream declares with a DEFAULT. PostgreSQL accepts any leading run of these
// positionally as well as by name; DuckDB never matches a named parameter positionally, so a spec
// row with k of them is registered as k + 1 variants, taking the first 0..k positionally. Kept as
// plain data (no LogicalType or Value) so the spec table needs no dynamic initialization that
// would depend on DuckDB's own static objects.
struct OptionalParam {
	const char *name;
	OptionalType type;
	int64_t default_value; // 0 or 1 for BOOLEAN
};

constexpr OptionalParam DIRECTED {"directed", OptionalType::BOOLEAN, 1};
constexpr OptionalParam CAP {"cap", OptionalType::BIGINT, 1};
constexpr OptionalParam GLOBAL {"global", OptionalType::BOOLEAN, 1};
constexpr OptionalParam DETAILS {"details", OptionalType::BOOLEAN, 0};

// Where an overload's driving side comes from. NONE: the overload has none (which = 0 ignores it).
// ARGUMENT: the CHAR signatures of the withPoints family take it as a required argument.
// FROM_DIRECTED: the other withPoints signatures pass (CASE WHEN directed THEN 'r' ELSE 'b' END).
enum class DrivingSideSource : uint8_t { NONE, ARGUMENT, FROM_DIRECTED };

// Which columns a public overload returns. The exec function always produces the eight path
// columns; COST is the projection upstream's Cost wrappers apply on top of the same driver call.
enum class ResultColumns : uint8_t {
	PATH, // seq, path_seq, start_vid, end_vid, node, edge, cost, agg_cost
	COST, // start_vid, end_vid, agg_cost
	// start_vid, end_vid, agg_cost of each path's closing row (edge = -1); the driver runs in
	// path mode. Works around the pinned pgRouting v4.0.2's only_cost Path constructor
	// (third_party/pgrouting/include/cpp_common/path.hpp), which leaves m_tot_cost
	// uninitialized; when n_goals (cap) is set, post_process's stable_sort/truncate by
	// tot_cost() then reads that garbage, so a NearCost overload run in only_cost mode can
	// return the wrong nearest destination. Upstream fixed this in commit b27576bd58 (not in
	// any released version yet): once the pinned release contains that fix, the NearCost
	// overloads can go back to only_cost = true with plain COST.
	COST_OF_PATH
};

// The driver flags that are fixed per overload rather than chosen by the caller. n_goals, global
// and details are only fallbacks: an overload that declares `cap`, `global` or `details` takes
// them from the call. driver selects pgRouting's unified do_shortestPath or one of the
// per-family drivers.
struct DriverFlags {
	bool only_cost = false;
	bool normal = true;
	int64_t n_goals = 0;
	bool global = false;
	DrivingSideSource driving_side = DrivingSideSource::NONE;
	bool details = true;
	int32_t which = 0;
	ResultColumns columns = ResultColumns::PATH;
	DriverKind driver = DriverKind::SHORTEST_PATH;
};

struct FunctionSpec {
	const char *upstream_name;
	duckdb::vector<ArgKind> args;
	duckdb::vector<OptionalParam> optionals; // upstream's declaration order
	DriverFlags flags;
};

// Defined in shortest_path_specs.cpp.
extern const duckdb::vector<FunctionSpec> SHORTEST_PATH_SPECS;

} // namespace duckdb_pgrouting
