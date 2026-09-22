// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table. Each row transcribes one CREATE FUNCTION of
// third_party/pgrouting/sql/dijkstra/*.sql or sql/withPoints/*.sql: its required arguments, its
// DEFAULT parameters in declaration order, and the constant flags its body passes to
// _pgr_dijkstra_v4 (edges, starts, ends, directed, only_cost, normal, n_goals, global) or, for a
// combinations signature, (edges, combinations, directed, only_cost, n_goals, global) with
// normal = true.
//
// Every public pgr_withPoints* function calls _pgr_withPoints_v4, whose C entry
// (src/withPoints/withPoints.c) passes which = 1 for the array and the combinations forms alike,
// and hardcodes n_goals = 0, global = true whatever the SQL wrapper passes.

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

// The flags a pgr_withPoints* wrapper passes (see the file-top comment). details = true is the
// fallback for overloads that have no details parameter: the Cost and CostMatrix wrappers pass a
// constant true.
DriverFlags WithPointsFlags(bool only_cost, bool normal, DrivingSideSource side, ResultColumns columns) {
	DriverFlags flags;
	flags.only_cost = only_cost;
	flags.normal = normal;
	flags.global = true;
	flags.driving_side = side;
	flags.details = true;
	flags.which = 1;
	flags.columns = columns;
	return flags;
}

constexpr auto CHAR_SIDE = DrivingSideSource::ARGUMENT;
constexpr auto SIDE_OF_DIRECTED = DrivingSideSource::FROM_DIRECTED;

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

    // pgr_dijkstraNear: cap is the driver's n_goals. The one-to-many and many-to-one bodies pass
    // a constant global = false; the other two expose global (default true).
    {"pgr_dijkstraNear", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VIDS}, {DIRECTED, CAP},
     Flags(false, true, false, ResultColumns::PATH)},
    {"pgr_dijkstraNear", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VID}, {DIRECTED, CAP},
     Flags(false, false, false, ResultColumns::PATH)},
    {"pgr_dijkstraNear", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS},
     {DIRECTED, CAP, GLOBAL}, Flags(false, true, true, ResultColumns::PATH)},
    {"pgr_dijkstraNear", {ArgKind::EDGES_SQL, ArgKind::COMBINATIONS_SQL}, {DIRECTED, CAP, GLOBAL},
     Flags(false, true, true, ResultColumns::PATH)},
    // pgr_dijkstraNearCost: as pgr_dijkstraNear, and its one-to-many and many-to-one bodies pass
    // a constant global = true (dijkstraNearCost.sql), not false, unlike pgr_dijkstraNear.
    // Upstream passes only_cost = true here; this extension deliberately passes false and
    // projects ResultColumns::COST_OF_PATH instead, because the pinned pgRouting v4.0.2's
    // only_cost Path constructor leaves m_tot_cost uninitialized, and post_process's cap-driven
    // sort/truncate by tot_cost() then picks the wrong nearest destination (see ResultColumns'
    // COST_OF_PATH comment in function_spec.hpp).
    {"pgr_dijkstraNearCost", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VIDS}, {DIRECTED, CAP},
     Flags(false, true, true, ResultColumns::COST_OF_PATH)},
    {"pgr_dijkstraNearCost", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VID}, {DIRECTED, CAP},
     Flags(false, false, true, ResultColumns::COST_OF_PATH)},
    {"pgr_dijkstraNearCost", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS},
     {DIRECTED, CAP, GLOBAL}, Flags(false, true, true, ResultColumns::COST_OF_PATH)},
    {"pgr_dijkstraNearCost", {ArgKind::EDGES_SQL, ArgKind::COMBINATIONS_SQL}, {DIRECTED, CAP, GLOBAL},
     Flags(false, true, true, ResultColumns::COST_OF_PATH)},

    // pgr_withPoints: details defaults to false. normal = false on many-to-one and many-to-many,
    // unlike pgr_dijkstra and pgr_withPointsCost (which pass it on many-to-one only).
    {"pgr_withPoints",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VID, ArgKind::END_VID, ArgKind::DRIVING_SIDE},
     {DIRECTED, DETAILS}, WithPointsFlags(false, true, CHAR_SIDE, ResultColumns::PATH)},
    {"pgr_withPoints",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VID, ArgKind::END_VIDS, ArgKind::DRIVING_SIDE},
     {DIRECTED, DETAILS}, WithPointsFlags(false, true, CHAR_SIDE, ResultColumns::PATH)},
    {"pgr_withPoints",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS, ArgKind::END_VID, ArgKind::DRIVING_SIDE},
     {DIRECTED, DETAILS}, WithPointsFlags(false, false, CHAR_SIDE, ResultColumns::PATH)},
    {"pgr_withPoints",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS, ArgKind::DRIVING_SIDE},
     {DIRECTED, DETAILS}, WithPointsFlags(false, false, CHAR_SIDE, ResultColumns::PATH)},
    {"pgr_withPoints",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::COMBINATIONS_SQL, ArgKind::DRIVING_SIDE},
     {DIRECTED, DETAILS}, WithPointsFlags(false, true, CHAR_SIDE, ResultColumns::PATH)},
    {"pgr_withPoints", {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VID, ArgKind::END_VID},
     {DIRECTED, DETAILS}, WithPointsFlags(false, true, SIDE_OF_DIRECTED, ResultColumns::PATH)},
    {"pgr_withPoints", {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VID, ArgKind::END_VIDS},
     {DIRECTED, DETAILS}, WithPointsFlags(false, true, SIDE_OF_DIRECTED, ResultColumns::PATH)},
    {"pgr_withPoints", {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS, ArgKind::END_VID},
     {DIRECTED, DETAILS}, WithPointsFlags(false, false, SIDE_OF_DIRECTED, ResultColumns::PATH)},
    {"pgr_withPoints", {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS},
     {DIRECTED, DETAILS}, WithPointsFlags(false, false, SIDE_OF_DIRECTED, ResultColumns::PATH)},
    {"pgr_withPoints", {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::COMBINATIONS_SQL},
     {DIRECTED, DETAILS}, WithPointsFlags(false, true, SIDE_OF_DIRECTED, ResultColumns::PATH)},

    // pgr_withPointsCost: only_cost, a constant details = true, no details parameter; normal =
    // false only on many-to-one (withPointsCost.sql), unlike pgr_dijkstraCost.
    {"pgr_withPointsCost",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VID, ArgKind::END_VID, ArgKind::DRIVING_SIDE},
     {DIRECTED}, WithPointsFlags(true, true, CHAR_SIDE, ResultColumns::COST)},
    {"pgr_withPointsCost",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VID, ArgKind::END_VIDS, ArgKind::DRIVING_SIDE},
     {DIRECTED}, WithPointsFlags(true, true, CHAR_SIDE, ResultColumns::COST)},
    {"pgr_withPointsCost",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS, ArgKind::END_VID, ArgKind::DRIVING_SIDE},
     {DIRECTED}, WithPointsFlags(true, false, CHAR_SIDE, ResultColumns::COST)},
    {"pgr_withPointsCost",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS, ArgKind::DRIVING_SIDE},
     {DIRECTED}, WithPointsFlags(true, true, CHAR_SIDE, ResultColumns::COST)},
    {"pgr_withPointsCost",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::COMBINATIONS_SQL, ArgKind::DRIVING_SIDE},
     {DIRECTED}, WithPointsFlags(true, true, CHAR_SIDE, ResultColumns::COST)},
    {"pgr_withPointsCost", {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VID, ArgKind::END_VID},
     {DIRECTED}, WithPointsFlags(true, true, SIDE_OF_DIRECTED, ResultColumns::COST)},
    {"pgr_withPointsCost", {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VID, ArgKind::END_VIDS},
     {DIRECTED}, WithPointsFlags(true, true, SIDE_OF_DIRECTED, ResultColumns::COST)},
    {"pgr_withPointsCost", {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS, ArgKind::END_VID},
     {DIRECTED}, WithPointsFlags(true, false, SIDE_OF_DIRECTED, ResultColumns::COST)},
    {"pgr_withPointsCost", {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS},
     {DIRECTED}, WithPointsFlags(true, true, SIDE_OF_DIRECTED, ResultColumns::COST)},
    {"pgr_withPointsCost", {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::COMBINATIONS_SQL},
     {DIRECTED}, WithPointsFlags(true, true, SIDE_OF_DIRECTED, ResultColumns::COST)},
    // pgr_withPointsCostMatrix: starts without ends, as pgr_dijkstraCostMatrix.
    {"pgr_withPointsCostMatrix",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS, ArgKind::DRIVING_SIDE},
     {DIRECTED}, WithPointsFlags(true, true, CHAR_SIDE, ResultColumns::COST)},
    {"pgr_withPointsCostMatrix", {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS},
     {DIRECTED}, WithPointsFlags(true, true, SIDE_OF_DIRECTED, ResultColumns::COST)},
};

} // namespace duckdb_routing
