// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of sql/driving_distance/*.sql. pgr_drivingDistance calls
// _pgr_drivingDistancev4(edges, roots, distance, directed, equicost) and pgr_withPointsDD calls
// _pgr_withPointsDDv4(edges, points, roots, distance, driving_side, directed, details, equicost);
// the one-root forms pass equicost = false, which is the request's default.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

namespace {

DriverFlags WithPointsDDFlags(DrivingSideSource side) {
	auto flags = FamilyFlags(DriverKind::WITH_POINTS_DD, false, Projection::ALL);
	flags.driving_side = side;
	return flags;
}

constexpr auto CHAR_SIDE = DrivingSideSource::ARGUMENT;
constexpr auto SIDE_OF_DIRECTED = DrivingSideSource::FROM_DIRECTED;

} // namespace

const duckdb::vector<FunctionSpec> DRIVING_DISTANCE_SPECS = {
    {"pgr_drivingDistance", {ArgKind::EDGES_SQL, ArgKind::ROOTS, ArgKind::DISTANCE}, {DIRECTED, EQUICOST},
     FamilyFlags(DriverKind::DRIVING_DISTANCE, false, Projection::ALL)},
    {"pgr_drivingDistance", {ArgKind::EDGES_SQL, ArgKind::ROOT, ArgKind::DISTANCE}, {DIRECTED},
     FamilyFlags(DriverKind::DRIVING_DISTANCE, false, Projection::ALL)},

    // pgr_withPointsDD: details defaults to false, as in pgr_withPoints.
    {"pgr_withPointsDD",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::ROOT, ArgKind::DISTANCE, ArgKind::DRIVING_SIDE},
     {DIRECTED, DETAILS}, WithPointsDDFlags(CHAR_SIDE)},
    {"pgr_withPointsDD",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::ROOTS, ArgKind::DISTANCE, ArgKind::DRIVING_SIDE},
     {DIRECTED, DETAILS, EQUICOST}, WithPointsDDFlags(CHAR_SIDE)},
    {"pgr_withPointsDD", {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::ROOT, ArgKind::DISTANCE},
     {DIRECTED, DETAILS}, WithPointsDDFlags(SIDE_OF_DIRECTED)},
    {"pgr_withPointsDD", {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::ROOTS, ArgKind::DISTANCE},
     {DIRECTED, DETAILS, EQUICOST}, WithPointsDDFlags(SIDE_OF_DIRECTED)},
};

} // namespace duckdb_pgrouting
