// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of sql/ordering/*.sql: the edge query only, and the vertices in their new order.
// Every C entry numbers the rows as a BIGINT; _pgr_topologicalSort declares its seq INTEGER, which
// the wrapper here reproduces with a cast.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

const duckdb::vector<FunctionSpec> ORDERING_SPECS = {
    {"pgr_cuthillMckeeOrdering", {ArgKind::EDGES_SQL}, {},
     ColumnsFlags(DriverKind::CUTHILL_MCKEE_ORDERING, "seq, id AS node")},
    {"pgr_kingOrdering", {ArgKind::EDGES_SQL}, {}, ColumnsFlags(DriverKind::KING_ORDERING, "seq, id AS node")},
    {"pgr_sloanOrdering", {ArgKind::EDGES_SQL}, {}, ColumnsFlags(DriverKind::SLOAN_ORDERING, "seq, id AS node")},
    {"pgr_topologicalSort", {ArgKind::EDGES_SQL}, {},
     ColumnsFlags(DriverKind::TOPOLOGICAL_SORT, "CAST(seq AS INTEGER) AS seq, id AS node")},
};

} // namespace duckdb_pgrouting
