// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of sql/lineGraph/*.sql: _pgr_lineGraph(edges, directed) and
// _pgr_lineGraphFull(edges), whose C entries' columns the wrappers return as they are.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

const duckdb::vector<FunctionSpec> LINE_GRAPH_SPECS = {
    {"pgr_lineGraph", {ArgKind::EDGES_SQL}, {DIRECTED}, FamilyFlags(DriverKind::LINE_GRAPH, false, Projection::ALL)},
    {"pgr_lineGraphFull", {ArgKind::EDGES_SQL}, {},
     FamilyFlags(DriverKind::LINE_GRAPH_FULL, false, Projection::ALL)},
};

} // namespace duckdb_pgrouting
