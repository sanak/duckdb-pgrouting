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
	VIDS,             // BIGINT[] passed as both the starts and the ends, as pgr_bdDijkstraCostMatrix does
	ROOT,             // BIGINT, a one-element roots list
	ROOTS,            // BIGINT[]
	DISTANCE,         // DOUBLE; upstream's FLOAT, and NUMERIC in pgr_kruskalDD / pgr_primDD
	K,                // INTEGER, the number of paths (upstream's INTEGER K)
	VIA,              // BIGINT[], the vertices a route visits in order
	RESTRICTIONS_SQL, // VARCHAR, the turn restrictions query (cost, path)
	MATRIX_SQL,       // VARCHAR, pgr_TSP's cost-matrix query (start_vid, end_vid, agg_cost)
	COORDINATES_SQL,  // VARCHAR, pgr_TSPeuclidean's coordinates query (id, x, y)
	ROOT_VID          // BIGINT, a single root vertex passed as the request's root
};

enum class OptionalType : uint8_t { BOOLEAN, BIGINT, INTEGER, DOUBLE };

// A parameter upstream declares with a DEFAULT. PostgreSQL accepts any leading run of these
// positionally as well as by name; DuckDB never matches a named parameter positionally, so a spec
// row with k of them is registered as k + 1 variants, taking the first 0..k positionally. Kept as
// plain data (no LogicalType or Value) so the spec tables need no dynamic initialization that
// would depend on DuckDB's own static objects.
struct OptionalParam {
	const char *name; // upstream's parameter name, which callers also pass by name
	OptionalType type;
	// Upstream's DEFAULT, spelled as the SQL literal it declares and cast to `type` at bind. Text,
	// not a number: max_depth's default is BIGINT's maximum, which a double cannot hold exactly.
	const char *default_value;
	const char *request_field; // the _pgr_exec request parameter it sets (src/exec/request_params.cpp)
};

constexpr OptionalParam DIRECTED {"directed", OptionalType::BOOLEAN, "true", "directed"};
constexpr OptionalParam CAP {"cap", OptionalType::BIGINT, "1", "n_goals"};
constexpr OptionalParam GLOBAL {"global", OptionalType::BOOLEAN, "true", "global"};
constexpr OptionalParam DETAILS {"details", OptionalType::BOOLEAN, "false", "details"};
constexpr OptionalParam HEURISTIC {"heuristic", OptionalType::INTEGER, "5", "heuristic"};
constexpr OptionalParam FACTOR {"factor", OptionalType::DOUBLE, "1.0", "factor"};
constexpr OptionalParam EPSILON {"epsilon", OptionalType::DOUBLE, "1.0", "epsilon"};
constexpr OptionalParam EQUICOST {"equicost", OptionalType::BOOLEAN, "false", "equicost"};
constexpr OptionalParam MAX_DEPTH {"max_depth", OptionalType::BIGINT, "9223372036854775807", "max_depth"};
constexpr OptionalParam HEAP_PATHS {"heap_paths", OptionalType::BOOLEAN, "false", "heap_paths"};
constexpr OptionalParam STOP_ON_FIRST {"stop_on_first", OptionalType::BOOLEAN, "true", "stop_on_first"};
// strict, as the Via families and pgr_turnRestrictedPath declare it. Not named STRICT: windows.h
// defines that as a macro.
constexpr OptionalParam VIA_STRICT {"strict", OptionalType::BOOLEAN, "false", "strict"};
// Upstream's spelling; DuckDB matches named parameters case-insensitively.
constexpr OptionalParam VIA_U_TURN_ON_EDGE {"U_turn_on_edge", OptionalType::BOOLEAN, "true", "u_turn_on_edge"};
// start_id and end_id, as the TSP functions declare them: 0 means "not given".
constexpr OptionalParam START_ID {"start_id", OptionalType::BIGINT, "0", "start_id"};
constexpr OptionalParam END_ID {"end_id", OptionalType::BIGINT, "0", "end_id"};

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
	COST_OF_PATH,
	// COST, ORDER BY start_vid, end_vid: pgr_aStarCost's wrapper sorts (sql/astar/astarCost.sql).
	COST_SORTED,
	// edge, cost of the MST shape: pgr_kruskal and pgr_prim (sql/spanningTree/kruskal.sql, prim.sql).
	EDGE_COST,
	// DriverFlags::columns, a select list over the driver's own columns: an overload over a generic
	// shape (IDS, ID_VALUE, TRIPLES, PAIRS) renames and picks them the way upstream's SQL wrapper
	// selects from its internal _pgr_* function.
	COLUMNS
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
	// The kruskal and prim families' suffix ("", "BFS", "DFS", "DD").
	const char *mst_suffix = "";
	// pgr_kruskal and pgr_prim pass ARRAY[0]::BIGINT[] as roots: their drivers read roots as a
	// non-empty array before anything else, and drop 0 from the answer.
	bool root_zero = false;
	// The max-flow family's algorithm (1 push-relabel, 2 Boykov-Kolmogorov, 3 Edmonds-Karp), a
	// constant in each upstream wrapper.
	int32_t algorithm = 1;
	Projection projection = Projection::ALL;
	// With Projection::COLUMNS, the outer SELECT list, written as SQL over the columns of the
	// driver's result shape (ShapeColumns). Spec-table text only, never a caller's: CheckSpec parses
	// it at load and checks every column it names.
	const char *columns = nullptr;
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

// A per-family wrapper that selects `columns` from the driver's result (Projection::COLUMNS).
inline DriverFlags ColumnsFlags(DriverKind driver, const char *columns) {
	auto flags = FamilyFlags(driver, false, Projection::COLUMNS);
	flags.columns = columns;
	return flags;
}

struct FunctionSpec {
	const char *upstream_name;
	duckdb::vector<ArgKind> args;
	duckdb::vector<OptionalParam> optionals; // upstream's declaration order
	DriverFlags flags;
	// Upstream returns one value rather than a set (RETURNS BIGINT, RETURNS BOOLEAN): the function
	// is then also registered as a same-name scalar macro over this one-row table function,
	// selecting this column, so that both SELECT pgr_x(...) and SELECT * FROM pgr_x(...) work. The
	// overload's column list must be exactly this one column. nullptr: the function returns a set.
	const char *scalar_column = nullptr;
};

// One table per upstream sql/ directory, each defined in the file named beside it.
// spec_functions.cpp lists them all.
extern const duckdb::vector<FunctionSpec> DIJKSTRA_SPECS;             // dijkstra_specs.cpp
extern const duckdb::vector<FunctionSpec> WITH_POINTS_SPECS;          // withpoints_specs.cpp
extern const duckdb::vector<FunctionSpec> BD_DIJKSTRA_SPECS;          // bd_dijkstra_specs.cpp
extern const duckdb::vector<FunctionSpec> BELLMAN_FORD_SPECS;         // bellman_ford_specs.cpp
extern const duckdb::vector<FunctionSpec> DAG_SHORTEST_PATH_SPECS;    // dag_shortest_path_specs.cpp
extern const duckdb::vector<FunctionSpec> DOMINATOR_SPECS;            // dominator_specs.cpp
extern const duckdb::vector<FunctionSpec> BREADTH_FIRST_SEARCH_SPECS; // breadth_first_search_specs.cpp
extern const duckdb::vector<FunctionSpec> CIRCUITS_SPECS;             // circuits_specs.cpp
extern const duckdb::vector<FunctionSpec> COMPONENTS_SPECS;           // components_specs.cpp
extern const duckdb::vector<FunctionSpec> COLORING_SPECS;             // coloring_specs.cpp
extern const duckdb::vector<FunctionSpec> ORDERING_SPECS;             // ordering_specs.cpp
extern const duckdb::vector<FunctionSpec> ALLPAIRS_SPECS;             // allpairs_specs.cpp
extern const duckdb::vector<FunctionSpec> METRICS_SPECS;              // metrics_specs.cpp
extern const duckdb::vector<FunctionSpec> MINCUT_SPECS;               // mincut_specs.cpp
extern const duckdb::vector<FunctionSpec> PLANAR_SPECS;               // planar_specs.cpp
extern const duckdb::vector<FunctionSpec> ASTAR_SPECS;                // astar_specs.cpp
extern const duckdb::vector<FunctionSpec> BD_ASTAR_SPECS;             // bd_astar_specs.cpp
extern const duckdb::vector<FunctionSpec> DRIVING_DISTANCE_SPECS;     // driving_distance_specs.cpp
extern const duckdb::vector<FunctionSpec> SPANNING_TREE_SPECS;        // spanning_tree_specs.cpp
extern const duckdb::vector<FunctionSpec> TRAVERSAL_SPECS;            // traversal_specs.cpp
extern const duckdb::vector<FunctionSpec> TRANSITIVE_CLOSURE_SPECS;   // transitive_closure_specs.cpp
extern const duckdb::vector<FunctionSpec> KSP_SPECS;                  // ksp_specs.cpp
extern const duckdb::vector<FunctionSpec> LINE_GRAPH_SPECS;           // line_graph_specs.cpp
extern const duckdb::vector<FunctionSpec> MAX_FLOW_SPECS;             // max_flow_specs.cpp
extern const duckdb::vector<FunctionSpec> TRSP_SPECS;                 // trsp_specs.cpp
extern const duckdb::vector<FunctionSpec> TSP_SPECS;                  // tsp_specs.cpp

} // namespace duckdb_pgrouting
