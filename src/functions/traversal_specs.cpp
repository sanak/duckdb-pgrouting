// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of sql/traversal/depthFirstSearch.sql: _pgr_depthFirstSearch(edges, roots,
// directed, max_depth).

#include "function_spec.hpp"

namespace duckdb_pgrouting {

const duckdb::vector<FunctionSpec> TRAVERSAL_SPECS = {
    {"pgr_depthFirstSearch", {ArgKind::EDGES_SQL, ArgKind::ROOT}, {DIRECTED, MAX_DEPTH},
     FamilyFlags(DriverKind::DEPTH_FIRST_SEARCH, false, Projection::ALL)},
    {"pgr_depthFirstSearch", {ArgKind::EDGES_SQL, ArgKind::ROOTS}, {DIRECTED, MAX_DEPTH},
     FamilyFlags(DriverKind::DEPTH_FIRST_SEARCH, false, Projection::ALL)},
};

} // namespace duckdb_pgrouting
