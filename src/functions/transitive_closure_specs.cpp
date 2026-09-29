// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of sql/transitiveClosure/*.sql: the wrapper selects (vid, target_array) of
// _pgr_transitiveClosure(edges) as (node, targets), dropping its seq.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

const duckdb::vector<FunctionSpec> TRANSITIVE_CLOSURE_SPECS = {
    {"pgr_transitiveClosure", {ArgKind::EDGES_SQL}, {},
     ColumnsFlags(DriverKind::TRANSITIVE_CLOSURE, "vid AS node, target_array AS targets")},
};

} // namespace duckdb_pgrouting
