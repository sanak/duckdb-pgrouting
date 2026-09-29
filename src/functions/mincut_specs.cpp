// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of sql/mincut/*.sql: _pgr_stoerWagner(edges), whose C entry's columns the
// wrapper returns as they are.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

const duckdb::vector<FunctionSpec> MINCUT_SPECS = {
    {"pgr_stoerWagner", {ArgKind::EDGES_SQL}, {}, FamilyFlags(DriverKind::STOER_WAGNER, false, Projection::ALL)},
};

} // namespace duckdb_pgrouting
