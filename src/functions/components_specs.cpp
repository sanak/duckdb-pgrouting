// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of sql/components/*.sql. None of these takes more than the edge query, and no
// C entry checks anything before its driver runs. pgr_connectedComponents and pgr_strongComponents
// return their driver's (seq, component, node) rows as they are; the others select from them the way
// their SQL wrapper selects from its internal _pgr_* function.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

const duckdb::vector<FunctionSpec> COMPONENTS_SPECS = {
    {"pgr_connectedComponents", {ArgKind::EDGES_SQL}, {},
     FamilyFlags(DriverKind::CONNECTED_COMPONENTS, false, Projection::ALL)},
    {"pgr_strongComponents", {ArgKind::EDGES_SQL}, {}, FamilyFlags(DriverKind::STRONG_COMPONENTS, false, Projection::ALL)},
    // node is an edge id here: upstream's wrapper names it edge.
    {"pgr_biconnectedComponents", {ArgKind::EDGES_SQL}, {},
     ColumnsFlags(DriverKind::BICONNECTED_COMPONENTS, "seq, component, node AS edge")},
    {"pgr_articulationPoints", {ArgKind::EDGES_SQL}, {}, ColumnsFlags(DriverKind::ARTICULATION_POINTS, "id AS node")},
    {"pgr_bridges", {ArgKind::EDGES_SQL}, {}, ColumnsFlags(DriverKind::BRIDGES, "id AS edge")},
    {"pgr_makeConnected", {ArgKind::EDGES_SQL}, {},
     ColumnsFlags(DriverKind::MAKE_CONNECTED, "seq, id AS start_vid, value AS end_vid")},
};

} // namespace duckdb_pgrouting
