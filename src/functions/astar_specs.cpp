// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of sql/astar/*.sql. Every wrapper calls _pgr_aStar(edges, starts, ends,
// directed, heuristic, factor, epsilon, only_cost, normal), or its combinations form, whose C entry
// (src/astar/astar.c) passes normal = true.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

namespace {

const duckdb::vector<OptionalParam> ASTAR_OPTIONALS = {DIRECTED, HEURISTIC, FACTOR, EPSILON};

} // namespace

const duckdb::vector<FunctionSpec> ASTAR_SPECS = {
    // pgr_aStar: many-to-one passes normal := false, as pgr_dijkstra does.
    {"pgr_aStar", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VID}, ASTAR_OPTIONALS,
     FamilyFlags(DriverKind::ASTAR, false, Projection::ALL)},
    {"pgr_aStar", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VIDS}, ASTAR_OPTIONALS,
     FamilyFlags(DriverKind::ASTAR, false, Projection::ALL)},
    {"pgr_aStar", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VID}, ASTAR_OPTIONALS,
     FamilyFlags(DriverKind::ASTAR, false, Projection::ALL, /* normal */ false)},
    {"pgr_aStar", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS}, ASTAR_OPTIONALS,
     FamilyFlags(DriverKind::ASTAR, false, Projection::ALL)},
    {"pgr_aStar", {ArgKind::EDGES_SQL, ArgKind::COMBINATIONS_SQL}, ASTAR_OPTIONALS,
     FamilyFlags(DriverKind::ASTAR, false, Projection::ALL)},

    // pgr_aStarCost: only_cost, sorted (astarCost.sql ends in ORDER BY a.start_vid, a.end_vid);
    // many-to-one passes normal := false like pgr_aStar.
    {"pgr_aStarCost", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VID}, ASTAR_OPTIONALS,
     FamilyFlags(DriverKind::ASTAR, true, Projection::COST_SORTED)},
    {"pgr_aStarCost", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VIDS}, ASTAR_OPTIONALS,
     FamilyFlags(DriverKind::ASTAR, true, Projection::COST_SORTED)},
    {"pgr_aStarCost", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VID}, ASTAR_OPTIONALS,
     FamilyFlags(DriverKind::ASTAR, true, Projection::COST_SORTED, /* normal */ false)},
    {"pgr_aStarCost", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS}, ASTAR_OPTIONALS,
     FamilyFlags(DriverKind::ASTAR, true, Projection::COST_SORTED)},
    {"pgr_aStarCost", {ArgKind::EDGES_SQL, ArgKind::COMBINATIONS_SQL}, ASTAR_OPTIONALS,
     FamilyFlags(DriverKind::ASTAR, true, Projection::COST_SORTED)},

    // pgr_aStarCostMatrix: the vertex array is both starts and ends (astarCostMatrix.sql passes
    // $2, $2), unsorted.
    {"pgr_aStarCostMatrix", {ArgKind::EDGES_SQL, ArgKind::VIDS}, ASTAR_OPTIONALS,
     FamilyFlags(DriverKind::ASTAR, true, Projection::COST)},
};

} // namespace duckdb_pgrouting
