// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table. Each row transcribes one CREATE FUNCTION of
// third_party/pgrouting/sql/dijkstra/*.sql: its required arguments, its DEFAULT parameters in
// declaration order, and the constant flags its body passes to _pgr_dijkstra_v4
// (edges, starts, ends, directed, only_cost, normal, n_goals, global) or, for a combinations
// signature, (edges, combinations, directed, only_cost, n_goals, global) with normal = true.

#include "function_spec.hpp"

namespace duckdb_routing {

namespace {

// The flags an upstream wrapper passes, by name; everything else keeps DriverFlags' defaults.
DriverFlags Flags(bool only_cost, bool normal, bool global, ResultColumns columns) {
	DriverFlags flags;
	flags.only_cost = only_cost;
	flags.normal = normal;
	flags.global = global;
	flags.columns = columns;
	return flags;
}

} // namespace

const duckdb::vector<FunctionSpec> SHORTEST_PATH_SPECS = {
    // pgr_dijkstra. normal = false on many-to-one is how upstream reverses the graph: fetch_edge
    // swaps source and target when normal is false, and post_process reverses the paths.
    {"pgr_dijkstra", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VID}, {DIRECTED}, {}},
    {"pgr_dijkstra", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VIDS}, {DIRECTED}, {}},
    {"pgr_dijkstra", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VID}, {DIRECTED},
     Flags(/* only_cost */ false, /* normal */ false, /* global */ false, ResultColumns::PATH)},
    {"pgr_dijkstra", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS}, {DIRECTED}, {}},
    {"pgr_dijkstra", {ArgKind::EDGES_SQL, ArgKind::COMBINATIONS_SQL}, {DIRECTED}, {}},

    // pgr_dijkstraCost: only_cost on every signature, and normal = true even on many-to-one
    // (dijkstraCost.sql passes `$4, true, true, 0, false` there, unlike pgr_dijkstra).
    {"pgr_dijkstraCost", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VID}, {DIRECTED},
     Flags(true, true, false, ResultColumns::COST)},
    {"pgr_dijkstraCost", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VIDS}, {DIRECTED},
     Flags(true, true, false, ResultColumns::COST)},
    {"pgr_dijkstraCost", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VID}, {DIRECTED},
     Flags(true, true, false, ResultColumns::COST)},
    {"pgr_dijkstraCost", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS}, {DIRECTED},
     Flags(true, true, false, ResultColumns::COST)},
    {"pgr_dijkstraCost", {ArgKind::EDGES_SQL, ArgKind::COMBINATIONS_SQL}, {DIRECTED},
     Flags(true, true, false, ResultColumns::COST)},
    // pgr_dijkstraCostMatrix: starts without ends; upstream's get_combinations pairs every start
    // with every other one when ends is empty.
    {"pgr_dijkstraCostMatrix", {ArgKind::EDGES_SQL, ArgKind::START_VIDS}, {DIRECTED},
     Flags(true, true, false, ResultColumns::COST)},
};

} // namespace duckdb_routing
