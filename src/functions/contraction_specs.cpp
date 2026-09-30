// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of sql/contraction/*.sql. pgr_contraction calls
// _pgr_contraction(edges, methods::BIGINT[], cycles, forbidden, directed) and selects its six
// columns unchanged. Its defaulted arrays reach the driver through the input row's methods and
// forbidden columns. pgr_contractionDeadEnd and pgr_contractionLinear call the same function with
// methods ARRAY[1] and ARRAY[2] and cycles 1.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

namespace {

// sql/contraction/deadEndContraction.sql and linearContraction.sql: a one-element methods array, one
// cycle (the request's default).
DriverFlags ContractionFlags(int32_t method) {
	auto flags = FamilyFlags(DriverKind::CONTRACTION, false, Projection::ALL);
	flags.contraction_method = method;
	return flags;
}

constexpr int32_t DEAD_END = 1;
constexpr int32_t LINEAR = 2;

} // namespace

const duckdb::vector<FunctionSpec> CONTRACTION_SPECS = {
    {"pgr_contraction",
     {ArgKind::EDGES_SQL},
     {DIRECTED, CONTRACTION_METHODS, CYCLES, FORBIDDEN},
     FamilyFlags(DriverKind::CONTRACTION, false, Projection::ALL)},
    {"pgr_contractionDeadEnd", {ArgKind::EDGES_SQL}, {DIRECTED, FORBIDDEN}, ContractionFlags(DEAD_END)},
    {"pgr_contractionLinear", {ArgKind::EDGES_SQL}, {DIRECTED, FORBIDDEN}, ContractionFlags(LINEAR)},
};

} // namespace duckdb_pgrouting
