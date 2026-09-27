// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of `sql/bdDijkstra/*.sql`. The wrappers pass only directed and only_cost; the
// driver has no n_goals, global, driving side or details.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

const duckdb::vector<FunctionSpec> BD_DIJKSTRA_SPECS = {
    // pgr_bdDijkstra: only_cost = false; many-to-one passes the arrays unswapped (no normal).
    {"pgr_bdDijkstra", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VID}, {DIRECTED},
     FamilyFlags(DriverKind::BD_DIJKSTRA, false, Projection::ALL)},
    {"pgr_bdDijkstra", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VIDS}, {DIRECTED},
     FamilyFlags(DriverKind::BD_DIJKSTRA, false, Projection::ALL)},
    {"pgr_bdDijkstra", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VID}, {DIRECTED},
     FamilyFlags(DriverKind::BD_DIJKSTRA, false, Projection::ALL)},
    {"pgr_bdDijkstra", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS}, {DIRECTED},
     FamilyFlags(DriverKind::BD_DIJKSTRA, false, Projection::ALL)},
    {"pgr_bdDijkstra", {ArgKind::EDGES_SQL, ArgKind::COMBINATIONS_SQL}, {DIRECTED},
     FamilyFlags(DriverKind::BD_DIJKSTRA, false, Projection::ALL)},

    // pgr_bdDijkstraCost: only_cost = true on every signature.
    {"pgr_bdDijkstraCost", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VID}, {DIRECTED},
     FamilyFlags(DriverKind::BD_DIJKSTRA, true, Projection::COST)},
    {"pgr_bdDijkstraCost", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VIDS}, {DIRECTED},
     FamilyFlags(DriverKind::BD_DIJKSTRA, true, Projection::COST)},
    {"pgr_bdDijkstraCost", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VID}, {DIRECTED},
     FamilyFlags(DriverKind::BD_DIJKSTRA, true, Projection::COST)},
    {"pgr_bdDijkstraCost", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS}, {DIRECTED},
     FamilyFlags(DriverKind::BD_DIJKSTRA, true, Projection::COST)},
    {"pgr_bdDijkstraCost", {ArgKind::EDGES_SQL, ArgKind::COMBINATIONS_SQL}, {DIRECTED},
     FamilyFlags(DriverKind::BD_DIJKSTRA, true, Projection::COST)},
    // pgr_bdDijkstraCostMatrix: the vertex array is both starts and ends
    // (bdDijkstraCostMatrix.sql), unlike pgr_dijkstraCostMatrix's starts without ends.
    {"pgr_bdDijkstraCostMatrix", {ArgKind::EDGES_SQL, ArgKind::VIDS}, {DIRECTED},
     FamilyFlags(DriverKind::BD_DIJKSTRA, true, Projection::COST)},
};

} // namespace duckdb_pgrouting
