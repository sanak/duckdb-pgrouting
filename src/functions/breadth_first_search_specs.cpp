// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of `sql/breadthFirstSearch/*.sql`.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

const duckdb::vector<FunctionSpec> BREADTH_FIRST_SEARCH_SPECS = {
    // pgr_binaryBreadthFirstSearch (sql/breadthFirstSearch): the driver takes no only_cost.
    {"pgr_binaryBreadthFirstSearch", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VID}, {DIRECTED},
     FamilyFlags(DriverKind::BINARY_BFS, false, Projection::ALL)},
    {"pgr_binaryBreadthFirstSearch", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VIDS}, {DIRECTED},
     FamilyFlags(DriverKind::BINARY_BFS, false, Projection::ALL)},
    {"pgr_binaryBreadthFirstSearch", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VID}, {DIRECTED},
     FamilyFlags(DriverKind::BINARY_BFS, false, Projection::ALL)},
    {"pgr_binaryBreadthFirstSearch", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS}, {DIRECTED},
     FamilyFlags(DriverKind::BINARY_BFS, false, Projection::ALL)},
    {"pgr_binaryBreadthFirstSearch", {ArgKind::EDGES_SQL, ArgKind::COMBINATIONS_SQL}, {DIRECTED},
     FamilyFlags(DriverKind::BINARY_BFS, false, Projection::ALL)},
    // pgr_breadthFirstSearch: _pgr_breadthFirstSearch(edges, roots, max_depth, directed).
    {"pgr_breadthFirstSearch", {ArgKind::EDGES_SQL, ArgKind::ROOT}, {DIRECTED, MAX_DEPTH},
     FamilyFlags(DriverKind::BREADTH_FIRST_SEARCH, false, Projection::ALL)},
    {"pgr_breadthFirstSearch", {ArgKind::EDGES_SQL, ArgKind::ROOTS}, {DIRECTED, MAX_DEPTH},
     FamilyFlags(DriverKind::BREADTH_FIRST_SEARCH, false, Projection::ALL)},
};

} // namespace duckdb_pgrouting
