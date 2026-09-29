// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of sql/dominator/*.sql: the wrapper selects (seq, vid, idom) of
// _pgr_lengauerTarjanDominatorTree(edges, root_vid), whose seq is an INTEGER. idom is the seq of the
// dominator's own row, as upstream emits it, not a vertex id.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

const duckdb::vector<FunctionSpec> DOMINATOR_SPECS = {
    {"pgr_lengauerTarjanDominatorTree", {ArgKind::EDGES_SQL, ArgKind::ROOT_VID}, {},
     ColumnsFlags(DriverKind::DOMINATOR_TREE, "CAST(seq AS INTEGER) AS seq, id AS vertex_id, value AS idom")},
};

} // namespace duckdb_pgrouting
