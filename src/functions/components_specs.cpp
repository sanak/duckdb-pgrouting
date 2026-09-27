// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of `sql/components/connectedComponents.sql`.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

const duckdb::vector<FunctionSpec> COMPONENTS_SPECS = {
    // pgr_connectedComponents: not a path function; its driver returns vertex/component pairs
    // (ResultShape::PAIRS). No starts or ends, no defaulted parameters.
    {"pgr_connectedComponents", {ArgKind::EDGES_SQL}, {},
     FamilyFlags(DriverKind::CONNECTED_COMPONENTS, false, Projection::ALL)},
};

} // namespace duckdb_pgrouting
