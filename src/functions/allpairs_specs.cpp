// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of sql/allpairs/*.sql: _pgr_johnson(edges, directed) and
// _pgr_floydWarshall(edges, directed), whose (from_vid, to_vid, cost) rows the wrappers name
// (start_vid, end_vid, agg_cost).

#include "function_spec.hpp"

namespace duckdb_pgrouting {

const duckdb::vector<FunctionSpec> ALLPAIRS_SPECS = {
    {"pgr_johnson", {ArgKind::EDGES_SQL}, {DIRECTED},
     ColumnsFlags(DriverKind::JOHNSON, "from_vid AS start_vid, to_vid AS end_vid, cost AS agg_cost")},
    {"pgr_floydWarshall", {ArgKind::EDGES_SQL}, {DIRECTED},
     ColumnsFlags(DriverKind::FLOYD_WARSHALL, "from_vid AS start_vid, to_vid AS end_vid, cost AS agg_cost")},
};

} // namespace duckdb_pgrouting
