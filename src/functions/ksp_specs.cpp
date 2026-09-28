// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of sql/ksp/ksp.sql and sql/ksp/withPointsKSP.sql (pgr_turnRestrictedPath, in
// the same directory, is not ported yet). pgr_ksp calls _pgr_ksp_v4(edges, starts, ends, K,
// directed, heap_paths) or its combinations form; a one-vertex argument becomes a one-element
// array. pgr_withPointsKSP calls _pgr_withPointsKSP_v4(edges, points, starts, ends, K,
// driving_side, directed, heap_paths, details) or its combinations form. The registered name
// pgr_ksp is the one sql/ksp/ksp.sql declares; upstream's documentation writes pgr_KSP, which
// DuckDB resolves to the same function.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

namespace {

DriverFlags KspFlags() {
	return FamilyFlags(DriverKind::KSP, false, Projection::ALL);
}

DriverFlags WithPointsKspFlags(DrivingSideSource side) {
	auto flags = FamilyFlags(DriverKind::WITH_POINTS_KSP, false, Projection::ALL);
	flags.driving_side = side;
	return flags;
}

constexpr auto CHAR_SIDE = DrivingSideSource::ARGUMENT;
constexpr auto SIDE_OF_DIRECTED = DrivingSideSource::FROM_DIRECTED;

} // namespace

const duckdb::vector<FunctionSpec> KSP_SPECS = {
    {"pgr_ksp", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VID, ArgKind::K}, {DIRECTED, HEAP_PATHS},
     KspFlags()},
    {"pgr_ksp", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VIDS, ArgKind::K}, {DIRECTED, HEAP_PATHS},
     KspFlags()},
    {"pgr_ksp", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VID, ArgKind::K}, {DIRECTED, HEAP_PATHS},
     KspFlags()},
    {"pgr_ksp", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS, ArgKind::K}, {DIRECTED, HEAP_PATHS},
     KspFlags()},
    {"pgr_ksp", {ArgKind::EDGES_SQL, ArgKind::COMBINATIONS_SQL, ArgKind::K}, {DIRECTED, HEAP_PATHS}, KspFlags()},

    // pgr_withPointsKSP: K comes before the driving side; details defaults to false.
    {"pgr_withPointsKSP",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VID, ArgKind::END_VID, ArgKind::K,
      ArgKind::DRIVING_SIDE},
     {DIRECTED, HEAP_PATHS, DETAILS}, WithPointsKspFlags(CHAR_SIDE)},
    {"pgr_withPointsKSP",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VID, ArgKind::END_VIDS, ArgKind::K,
      ArgKind::DRIVING_SIDE},
     {DIRECTED, HEAP_PATHS, DETAILS}, WithPointsKspFlags(CHAR_SIDE)},
    {"pgr_withPointsKSP",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS, ArgKind::END_VID, ArgKind::K,
      ArgKind::DRIVING_SIDE},
     {DIRECTED, HEAP_PATHS, DETAILS}, WithPointsKspFlags(CHAR_SIDE)},
    {"pgr_withPointsKSP",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS, ArgKind::K,
      ArgKind::DRIVING_SIDE},
     {DIRECTED, HEAP_PATHS, DETAILS}, WithPointsKspFlags(CHAR_SIDE)},
    {"pgr_withPointsKSP",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::COMBINATIONS_SQL, ArgKind::K, ArgKind::DRIVING_SIDE},
     {DIRECTED, HEAP_PATHS, DETAILS}, WithPointsKspFlags(CHAR_SIDE)},
    {"pgr_withPointsKSP",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VID, ArgKind::END_VID, ArgKind::K},
     {DIRECTED, HEAP_PATHS, DETAILS}, WithPointsKspFlags(SIDE_OF_DIRECTED)},
    {"pgr_withPointsKSP",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VID, ArgKind::END_VIDS, ArgKind::K},
     {DIRECTED, HEAP_PATHS, DETAILS}, WithPointsKspFlags(SIDE_OF_DIRECTED)},
    {"pgr_withPointsKSP",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS, ArgKind::END_VID, ArgKind::K},
     {DIRECTED, HEAP_PATHS, DETAILS}, WithPointsKspFlags(SIDE_OF_DIRECTED)},
    {"pgr_withPointsKSP",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS, ArgKind::K},
     {DIRECTED, HEAP_PATHS, DETAILS}, WithPointsKspFlags(SIDE_OF_DIRECTED)},
    {"pgr_withPointsKSP", {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::COMBINATIONS_SQL, ArgKind::K},
     {DIRECTED, HEAP_PATHS, DETAILS}, WithPointsKspFlags(SIDE_OF_DIRECTED)},
};

} // namespace duckdb_pgrouting
