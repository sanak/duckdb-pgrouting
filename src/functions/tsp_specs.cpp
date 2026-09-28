// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of sql/tsp/*.sql. pgr_TSP calls _pgr_TSP_v4(matrix, start_id, end_id) and
// pgr_TSPeuclidean calls _pgr_TSPeuclidean_v4(coordinates, start_id, end_id); neither takes an edge
// query, and neither C entry checks its parameters.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

const duckdb::vector<FunctionSpec> TSP_SPECS = {
    {"pgr_TSP", {ArgKind::MATRIX_SQL}, {START_ID, END_ID}, FamilyFlags(DriverKind::TSP, false, Projection::ALL)},
    {"pgr_TSPeuclidean", {ArgKind::COORDINATES_SQL}, {START_ID, END_ID},
     FamilyFlags(DriverKind::EUCLIDEAN_TSP, false, Projection::ALL)},
};

} // namespace duckdb_pgrouting
