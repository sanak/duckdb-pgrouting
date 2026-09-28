// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of `sql/dijkstra/*.sql`. Each row transcribes one CREATE FUNCTION: its
// required arguments, its DEFAULT parameters in declaration order, and the constant flags its body
// passes to _pgr_dijkstra_v4 (edges, starts, ends, directed, only_cost, normal, n_goals, global)
// or, for a combinations signature, (edges, combinations, directed, only_cost, n_goals, global)
// with normal = true. pgr_dijkstraVia calls _pgr_dijkstraVia(edges, via, directed, strict,
// U_turn_on_edge), a per-family driver of its own.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

namespace {

// The flags an upstream wrapper passes, by name; everything else keeps DriverFlags' defaults.
DriverFlags Flags(bool only_cost, bool normal, bool global, Projection projection) {
	DriverFlags flags;
	flags.only_cost = only_cost;
	flags.normal = normal;
	flags.global = global;
	flags.projection = projection;
	return flags;
}

} // namespace

const duckdb::vector<FunctionSpec> DIJKSTRA_SPECS = {
    // pgr_dijkstra. normal = false on many-to-one is how upstream reverses the graph: fetch_edge
    // swaps source and target when normal is false, and post_process reverses the paths.
    {"pgr_dijkstra", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VID}, {DIRECTED}, {}},
    {"pgr_dijkstra", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VIDS}, {DIRECTED}, {}},
    {"pgr_dijkstra", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VID}, {DIRECTED},
     Flags(/* only_cost */ false, /* normal */ false, /* global */ false, Projection::ALL)},
    {"pgr_dijkstra", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS}, {DIRECTED}, {}},
    {"pgr_dijkstra", {ArgKind::EDGES_SQL, ArgKind::COMBINATIONS_SQL}, {DIRECTED}, {}},

    // pgr_dijkstraCost: only_cost on every signature, and normal = true even on many-to-one
    // (dijkstraCost.sql passes `$4, true, true, 0, false` there, unlike pgr_dijkstra).
    {"pgr_dijkstraCost", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VID}, {DIRECTED},
     Flags(true, true, false, Projection::COST)},
    {"pgr_dijkstraCost", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VIDS}, {DIRECTED},
     Flags(true, true, false, Projection::COST)},
    {"pgr_dijkstraCost", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VID}, {DIRECTED},
     Flags(true, true, false, Projection::COST)},
    {"pgr_dijkstraCost", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS}, {DIRECTED},
     Flags(true, true, false, Projection::COST)},
    {"pgr_dijkstraCost", {ArgKind::EDGES_SQL, ArgKind::COMBINATIONS_SQL}, {DIRECTED},
     Flags(true, true, false, Projection::COST)},
    // pgr_dijkstraCostMatrix: starts without ends; upstream's get_combinations pairs every start
    // with every other one when ends is empty.
    {"pgr_dijkstraCostMatrix", {ArgKind::EDGES_SQL, ArgKind::START_VIDS}, {DIRECTED},
     Flags(true, true, false, Projection::COST)},

    // pgr_dijkstraNear: cap is the driver's n_goals. The one-to-many and many-to-one bodies pass
    // a constant global = false; the other two expose global (default true).
    {"pgr_dijkstraNear", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VIDS}, {DIRECTED, CAP},
     Flags(false, true, false, Projection::ALL)},
    {"pgr_dijkstraNear", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VID}, {DIRECTED, CAP},
     Flags(false, false, false, Projection::ALL)},
    {"pgr_dijkstraNear", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS},
     {DIRECTED, CAP, GLOBAL}, Flags(false, true, true, Projection::ALL)},
    {"pgr_dijkstraNear", {ArgKind::EDGES_SQL, ArgKind::COMBINATIONS_SQL}, {DIRECTED, CAP, GLOBAL},
     Flags(false, true, true, Projection::ALL)},
    // pgr_dijkstraNearCost: as pgr_dijkstraNear, and its one-to-many and many-to-one bodies pass
    // a constant global = true (dijkstraNearCost.sql), not false, unlike pgr_dijkstraNear.
    // Upstream passes only_cost = true here; this extension deliberately passes false and
    // projects Projection::COST_OF_PATH instead, because the pinned pgRouting v4.0.2's
    // only_cost Path constructor leaves m_tot_cost uninitialized, and post_process's cap-driven
    // sort/truncate by tot_cost() then picks the wrong nearest destination (see Projection's
    // COST_OF_PATH comment in function_spec.hpp).
    {"pgr_dijkstraNearCost", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VIDS}, {DIRECTED, CAP},
     Flags(false, true, true, Projection::COST_OF_PATH)},
    {"pgr_dijkstraNearCost", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VID}, {DIRECTED, CAP},
     Flags(false, false, true, Projection::COST_OF_PATH)},
    {"pgr_dijkstraNearCost", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS},
     {DIRECTED, CAP, GLOBAL}, Flags(false, true, true, Projection::COST_OF_PATH)},
    {"pgr_dijkstraNearCost", {ArgKind::EDGES_SQL, ArgKind::COMBINATIONS_SQL}, {DIRECTED, CAP, GLOBAL},
     Flags(false, true, true, Projection::COST_OF_PATH)},

    {"pgr_dijkstraVia", {ArgKind::EDGES_SQL, ArgKind::VIA}, {DIRECTED, VIA_STRICT, VIA_U_TURN_ON_EDGE},
     FamilyFlags(DriverKind::DIJKSTRA_VIA, false, Projection::ALL)},
};

} // namespace duckdb_pgrouting
