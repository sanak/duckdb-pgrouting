// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of sql/metrics/*.sql: _pgr_betweennessCentrality(edges, directed), whose driver
// fills an IID_t_rt with the vertex in from_vid and its centrality in cost, and _pgr_bandwidth(edges),
// whose one value the driver returns as a single IDS row.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

const duckdb::vector<FunctionSpec> METRICS_SPECS = {
    {"pgr_betweennessCentrality", {ArgKind::EDGES_SQL}, {DIRECTED},
     ColumnsFlags(DriverKind::BETWEENNESS_CENTRALITY, "from_vid AS vid, cost AS centrality")},
    {"pgr_bandwidth", {ArgKind::EDGES_SQL}, {}, ColumnsFlags(DriverKind::BANDWIDTH, "id AS bandwidth"), "bandwidth"},
};

} // namespace duckdb_pgrouting
