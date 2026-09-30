// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of sql/contraction/*.sql. pgr_contraction calls
// _pgr_contraction(edges, methods::BIGINT[], cycles, forbidden, directed) and selects its six
// columns unchanged. Its defaulted arrays reach the driver through the input row's methods and
// forbidden columns.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

const duckdb::vector<FunctionSpec> CONTRACTION_SPECS = {
    {"pgr_contraction",
     {ArgKind::EDGES_SQL},
     {DIRECTED, CONTRACTION_METHODS, CYCLES, FORBIDDEN},
     FamilyFlags(DriverKind::CONTRACTION, false, Projection::ALL)},
};

} // namespace duckdb_pgrouting
