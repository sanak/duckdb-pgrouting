// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of sql/circuits/*.sql: _pgr_hawickCircuits(edges), whose C entry's columns the
// wrapper returns as they are.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

const duckdb::vector<FunctionSpec> CIRCUITS_SPECS = {
    {"pgr_hawickCircuits", {ArgKind::EDGES_SQL}, {},
     FamilyFlags(DriverKind::HAWICK_CIRCUITS, false, Projection::ALL)},
};

} // namespace duckdb_pgrouting
