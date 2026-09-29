// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of sql/coloring/*.sql: the edge query only, and a (vertex or edge, color) pair
// per row, which each wrapper selects from its internal _pgr_* function under its own names.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

const duckdb::vector<FunctionSpec> COLORING_SPECS = {
    {"pgr_sequentialVertexColoring", {ArgKind::EDGES_SQL}, {},
     ColumnsFlags(DriverKind::SEQUENTIAL_VERTEX_COLORING, "id AS node, value AS color")},
    {"pgr_bipartite", {ArgKind::EDGES_SQL}, {}, ColumnsFlags(DriverKind::BIPARTITE, "id AS node, value AS color")},
    {"pgr_edgeColoring", {ArgKind::EDGES_SQL}, {}, ColumnsFlags(DriverKind::EDGE_COLORING, "id AS edge, value AS color")},
};

} // namespace duckdb_pgrouting
