// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of `sql/dagShortestPath/*.sql`.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

const duckdb::vector<FunctionSpec> DAG_SHORTEST_PATH_SPECS = {
    // pgr_dagShortestPath: no directed and no defaulted parameter; only_cost = false and
    // normal = true on every signature, many-to-one included (dagShortestPath.sql; the
    // combinations C entry passes normal = true).
    {"pgr_dagShortestPath", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VID}, {},
     FamilyFlags(DriverKind::DAG_SHORTEST_PATH, false, Projection::ALL)},
    {"pgr_dagShortestPath", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VIDS}, {},
     FamilyFlags(DriverKind::DAG_SHORTEST_PATH, false, Projection::ALL)},
    {"pgr_dagShortestPath", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VID}, {},
     FamilyFlags(DriverKind::DAG_SHORTEST_PATH, false, Projection::ALL)},
    {"pgr_dagShortestPath", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS}, {},
     FamilyFlags(DriverKind::DAG_SHORTEST_PATH, false, Projection::ALL)},
    {"pgr_dagShortestPath", {ArgKind::EDGES_SQL, ArgKind::COMBINATIONS_SQL}, {},
     FamilyFlags(DriverKind::DAG_SHORTEST_PATH, false, Projection::ALL)},
};

} // namespace duckdb_pgrouting
