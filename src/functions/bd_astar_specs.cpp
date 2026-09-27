// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of sql/bdAstar/*.sql. Every wrapper calls _pgr_bdAstar(edges, starts, ends,
// directed, heuristic, factor, epsilon, only_cost) or its combinations form; there is no normal,
// so many-to-one passes the arrays as given. Upstream declares factor and epsilon NUMERIC here and
// casts them to FLOAT for the C call; they are DOUBLE in DuckDB.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

namespace {

const duckdb::vector<OptionalParam> BD_ASTAR_OPTIONALS = {DIRECTED, HEURISTIC, FACTOR, EPSILON};

} // namespace

const duckdb::vector<FunctionSpec> BD_ASTAR_SPECS = {
    {"pgr_bdAstar", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VID}, BD_ASTAR_OPTIONALS,
     FamilyFlags(DriverKind::BD_ASTAR, false, Projection::ALL)},
    {"pgr_bdAstar", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VIDS}, BD_ASTAR_OPTIONALS,
     FamilyFlags(DriverKind::BD_ASTAR, false, Projection::ALL)},
    {"pgr_bdAstar", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VID}, BD_ASTAR_OPTIONALS,
     FamilyFlags(DriverKind::BD_ASTAR, false, Projection::ALL)},
    {"pgr_bdAstar", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS}, BD_ASTAR_OPTIONALS,
     FamilyFlags(DriverKind::BD_ASTAR, false, Projection::ALL)},
    {"pgr_bdAstar", {ArgKind::EDGES_SQL, ArgKind::COMBINATIONS_SQL}, BD_ASTAR_OPTIONALS,
     FamilyFlags(DriverKind::BD_ASTAR, false, Projection::ALL)},

    // pgr_bdAstarCost: only_cost; no ORDER BY in its wrapper, unlike pgr_aStarCost.
    {"pgr_bdAstarCost", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VID}, BD_ASTAR_OPTIONALS,
     FamilyFlags(DriverKind::BD_ASTAR, true, Projection::COST)},
    {"pgr_bdAstarCost", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VIDS}, BD_ASTAR_OPTIONALS,
     FamilyFlags(DriverKind::BD_ASTAR, true, Projection::COST)},
    {"pgr_bdAstarCost", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VID}, BD_ASTAR_OPTIONALS,
     FamilyFlags(DriverKind::BD_ASTAR, true, Projection::COST)},
    {"pgr_bdAstarCost", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS}, BD_ASTAR_OPTIONALS,
     FamilyFlags(DriverKind::BD_ASTAR, true, Projection::COST)},
    {"pgr_bdAstarCost", {ArgKind::EDGES_SQL, ArgKind::COMBINATIONS_SQL}, BD_ASTAR_OPTIONALS,
     FamilyFlags(DriverKind::BD_ASTAR, true, Projection::COST)},

    // pgr_bdAstarCostMatrix: the vertex array is both starts and ends (bdAstarCostMatrix.sql).
    {"pgr_bdAstarCostMatrix", {ArgKind::EDGES_SQL, ArgKind::VIDS}, BD_ASTAR_OPTIONALS,
     FamilyFlags(DriverKind::BD_ASTAR, true, Projection::COST)},
};

} // namespace duckdb_pgrouting
