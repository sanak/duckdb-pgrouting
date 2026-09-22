// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table. Each row transcribes one CREATE FUNCTION of
// third_party/pgrouting/sql/dijkstra/*.sql: its required arguments, its DEFAULT parameters in
// declaration order, and the constant flags its body passes to _pgr_dijkstra_v4
// (edges, starts, ends, directed, only_cost, normal, n_goals, global) or, for a combinations
// signature, (edges, combinations, directed, only_cost, n_goals, global) with normal = true.
//
// C++17 has no designated initializers, so DriverFlags is filled positionally where it differs
// from its defaults. The field names in the comments follow function_spec.hpp's declaration order:
// a field inserted there then shows up here as names that no longer match their values, instead
// of silently re-mapping every one of them.

#include "function_spec.hpp"

namespace duckdb_routing {

const duckdb::vector<FunctionSpec> SHORTEST_PATH_SPECS = {
    // pgr_dijkstra. normal = false on many-to-one is how upstream reverses the graph: fetch_edge
    // swaps source and target when normal is false, and post_process reverses the paths.
    {"pgr_dijkstra", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VID}, {DIRECTED}, {}},
    {"pgr_dijkstra", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VIDS}, {DIRECTED}, {}},
    {"pgr_dijkstra",
     {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VID},
     {DIRECTED},
     {/* only_cost */ false, /* normal */ false, /* n_goals */ 0, /* global */ false,
      /* driving_side */ ' ', /* details */ true, /* which */ 0, /* result_kind */ "path"}},
    {"pgr_dijkstra", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS}, {DIRECTED}, {}},
    {"pgr_dijkstra", {ArgKind::EDGES_SQL, ArgKind::COMBINATIONS_SQL}, {DIRECTED}, {}},
};

} // namespace duckdb_routing
