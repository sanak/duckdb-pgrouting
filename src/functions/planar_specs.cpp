// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of sql/planar/*.sql: _pgr_isPlanar(edges) RETURNS BOOLEAN, whose driver's bool
// arrives as a single IDS row. Without an OUT parameter PostgreSQL names the column after the
// function, lower-cased.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

const duckdb::vector<FunctionSpec> PLANAR_SPECS = {
    {"pgr_isPlanar", {ArgKind::EDGES_SQL}, {},
     ColumnsFlags(DriverKind::IS_PLANAR, "CAST(id AS BOOLEAN) AS pgr_isplanar"), "pgr_isplanar"},
};

} // namespace duckdb_pgrouting
