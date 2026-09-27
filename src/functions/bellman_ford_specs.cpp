// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of `sql/bellman_ford/*.sql`: pgr_bellmanFord and pgr_edwardMoore. The
// wrappers pass only directed and only_cost.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

const duckdb::vector<FunctionSpec> BELLMAN_FORD_SPECS = {
    // pgr_bellmanFord: only_cost = false (bellman_ford.sql); no Cost variant exists.
    {"pgr_bellmanFord", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VID}, {DIRECTED},
     FamilyFlags(DriverKind::BELLMAN_FORD, false, Projection::ALL)},
    {"pgr_bellmanFord", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VIDS}, {DIRECTED},
     FamilyFlags(DriverKind::BELLMAN_FORD, false, Projection::ALL)},
    {"pgr_bellmanFord", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VID}, {DIRECTED},
     FamilyFlags(DriverKind::BELLMAN_FORD, false, Projection::ALL)},
    {"pgr_bellmanFord", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS}, {DIRECTED},
     FamilyFlags(DriverKind::BELLMAN_FORD, false, Projection::ALL)},
    {"pgr_bellmanFord", {ArgKind::EDGES_SQL, ArgKind::COMBINATIONS_SQL}, {DIRECTED},
     FamilyFlags(DriverKind::BELLMAN_FORD, false, Projection::ALL)},

    // pgr_edwardMoore: the driver takes no only_cost.
    {"pgr_edwardMoore", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VID}, {DIRECTED},
     FamilyFlags(DriverKind::EDWARD_MOORE, false, Projection::ALL)},
    {"pgr_edwardMoore", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VIDS}, {DIRECTED},
     FamilyFlags(DriverKind::EDWARD_MOORE, false, Projection::ALL)},
    {"pgr_edwardMoore", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VID}, {DIRECTED},
     FamilyFlags(DriverKind::EDWARD_MOORE, false, Projection::ALL)},
    {"pgr_edwardMoore", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS}, {DIRECTED},
     FamilyFlags(DriverKind::EDWARD_MOORE, false, Projection::ALL)},
    {"pgr_edwardMoore", {ArgKind::EDGES_SQL, ArgKind::COMBINATIONS_SQL}, {DIRECTED},
     FamilyFlags(DriverKind::EDWARD_MOORE, false, Projection::ALL)},
};

} // namespace duckdb_pgrouting
