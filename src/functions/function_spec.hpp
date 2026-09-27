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
// plain data (no LogicalType or Value) so the spec tables need no dynamic initialization that
// would depend on DuckDB's own static objects.
struct OptionalParam {
	const char *name; // upstream's parameter name, which callers also pass by name
	OptionalType type;
	int64_t default_value;     // 0 or 1 for BOOLEAN
	const char *request_field; // the _pgr_exec request parameter it sets (src/exec/request_params.cpp)
};

constexpr OptionalParam DIRECTED {"directed", OptionalType::BOOLEAN, 1, "directed"};
constexpr OptionalParam CAP {"cap", OptionalType::BIGINT, 1, "n_goals"};
constexpr OptionalParam GLOBAL {"global", OptionalType::BOOLEAN, 1, "global"};
constexpr OptionalParam DETAILS {"details", OptionalType::BOOLEAN, 0, "details"};

// Where an overload's driving side comes from. NONE: the overload has none (which = 0 ignores it).
// ARGUMENT: the CHAR signatures of the withPoints family take it as a required argument.
// FROM_DIRECTED: the other withPoints signatures pass (CASE WHEN directed THEN 'r' ELSE 'b' END).
enum class DrivingSideSource : uint8_t { NONE, ARGUMENT, FROM_DIRECTED };

// Which of _pgr_exec's columns a public overload returns. ALL is the driver's whole result shape
// (src/exec/result_emitters.cpp). The others project the PATH shape the way upstream's Cost
// wrappers do on top of the same driver call.
enum class Projection : uint8_t {
	ALL,
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

// The request fields that are fixed per overload rather than chosen by the caller. n_goals, global
// and details are only fallbacks: an overload that declares `cap`, `global` or `details` takes
// them from the call. driver selects pgRouting's unified do_shortestPath or one of the per-family
// drivers, and with it the result shape.
struct DriverFlags {
	bool only_cost = false;
	bool normal = true;
	int64_t n_goals = 0;
	bool global = false;
	DrivingSideSource driving_side = DrivingSideSource::NONE;
	bool details = true;
	int32_t which = 0;
	Projection projection = Projection::ALL;
	DriverKind driver = DriverKind::SHORTEST_PATH;
};

// The flags a per-family wrapper passes (every driver but SHORTEST_PATH). normal is read only by
// the drivers whose C entry takes it.
inline DriverFlags FamilyFlags(DriverKind driver, bool only_cost, Projection projection, bool normal = true) {
	DriverFlags flags;
	flags.only_cost = only_cost;
	flags.normal = normal;
	flags.projection = projection;
	flags.driver = driver;
	return flags;
}

struct FunctionSpec {
	const char *upstream_name;
	duckdb::vector<ArgKind> args;
	duckdb::vector<OptionalParam> optionals; // upstream's declaration order
	DriverFlags flags;
};

// One table per upstream sql/ directory, each defined in the file named beside it.
// spec_functions.cpp lists them all.
extern const duckdb::vector<FunctionSpec> DIJKSTRA_SPECS;             // dijkstra_specs.cpp
extern const duckdb::vector<FunctionSpec> WITH_POINTS_SPECS;          // withpoints_specs.cpp
extern const duckdb::vector<FunctionSpec> BD_DIJKSTRA_SPECS;          // bd_dijkstra_specs.cpp
extern const duckdb::vector<FunctionSpec> BELLMAN_FORD_SPECS;         // bellman_ford_specs.cpp
extern const duckdb::vector<FunctionSpec> DAG_SHORTEST_PATH_SPECS;    // dag_shortest_path_specs.cpp
extern const duckdb::vector<FunctionSpec> BREADTH_FIRST_SEARCH_SPECS; // breadth_first_search_specs.cpp
extern const duckdb::vector<FunctionSpec> COMPONENTS_SPECS;           // components_specs.cpp

} // namespace duckdb_pgrouting
