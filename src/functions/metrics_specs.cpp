// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of sql/metrics/*.sql: _pgr_betweennessCentrality(edges, directed), whose driver
// fills an IID_t_rt with the vertex in from_vid and its centrality in cost.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

const duckdb::vector<FunctionSpec> METRICS_SPECS = {
    {"pgr_betweennessCentrality", {ArgKind::EDGES_SQL}, {DIRECTED},
     ColumnsFlags(DriverKind::BETWEENNESS_CENTRALITY, "from_vid AS vid, cost AS centrality")},
};

} // namespace duckdb_pgrouting
