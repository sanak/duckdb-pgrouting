// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of sql/trsp/*.sql. pgr_trsp calls _pgr_trspv4(edges, restrictions, starts,
// ends, directed) or its combinations form; a one-vertex argument becomes a one-element array.
// pgr_trsp_withPoints calls _pgr_trsp_withPoints_v4(edges, restrictions, points, starts, ends,
// directed, driving_side, details) or its combinations form.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

namespace {

DriverFlags TrspFlags() {
	return FamilyFlags(DriverKind::TRSP, false, Projection::ALL);
}

DriverFlags TrspWithPointsFlags(DrivingSideSource side) {
	auto flags = FamilyFlags(DriverKind::TRSP_WITH_POINTS, false, Projection::ALL);
	flags.driving_side = side;
	return flags;
}

constexpr auto CHAR_SIDE = DrivingSideSource::ARGUMENT;
constexpr auto SIDE_OF_DIRECTED = DrivingSideSource::FROM_DIRECTED;

} // namespace

const duckdb::vector<FunctionSpec> TRSP_SPECS = {
    {"pgr_trsp", {ArgKind::EDGES_SQL, ArgKind::RESTRICTIONS_SQL, ArgKind::START_VID, ArgKind::END_VID}, {DIRECTED},
     TrspFlags()},
    {"pgr_trsp", {ArgKind::EDGES_SQL, ArgKind::RESTRICTIONS_SQL, ArgKind::START_VID, ArgKind::END_VIDS}, {DIRECTED},
     TrspFlags()},
    {"pgr_trsp", {ArgKind::EDGES_SQL, ArgKind::RESTRICTIONS_SQL, ArgKind::START_VIDS, ArgKind::END_VID}, {DIRECTED},
     TrspFlags()},
    {"pgr_trsp", {ArgKind::EDGES_SQL, ArgKind::RESTRICTIONS_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS}, {DIRECTED},
     TrspFlags()},
    {"pgr_trsp", {ArgKind::EDGES_SQL, ArgKind::RESTRICTIONS_SQL, ArgKind::COMBINATIONS_SQL}, {DIRECTED}, TrspFlags()},

    // pgr_trsp_withPoints: the driving side follows the vertices; details defaults to false.
    {"pgr_trsp_withPoints",
     {ArgKind::EDGES_SQL, ArgKind::RESTRICTIONS_SQL, ArgKind::POINTS_SQL, ArgKind::START_VID, ArgKind::END_VID,
      ArgKind::DRIVING_SIDE},
     {DIRECTED, DETAILS}, TrspWithPointsFlags(CHAR_SIDE)},
    {"pgr_trsp_withPoints",
     {ArgKind::EDGES_SQL, ArgKind::RESTRICTIONS_SQL, ArgKind::POINTS_SQL, ArgKind::START_VID, ArgKind::END_VIDS,
      ArgKind::DRIVING_SIDE},
     {DIRECTED, DETAILS}, TrspWithPointsFlags(CHAR_SIDE)},
    {"pgr_trsp_withPoints",
     {ArgKind::EDGES_SQL, ArgKind::RESTRICTIONS_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS, ArgKind::END_VID,
      ArgKind::DRIVING_SIDE},
     {DIRECTED, DETAILS}, TrspWithPointsFlags(CHAR_SIDE)},
    {"pgr_trsp_withPoints",
     {ArgKind::EDGES_SQL, ArgKind::RESTRICTIONS_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS,
      ArgKind::DRIVING_SIDE},
     {DIRECTED, DETAILS}, TrspWithPointsFlags(CHAR_SIDE)},
    {"pgr_trsp_withPoints",
     {ArgKind::EDGES_SQL, ArgKind::RESTRICTIONS_SQL, ArgKind::POINTS_SQL, ArgKind::COMBINATIONS_SQL,
      ArgKind::DRIVING_SIDE},
     {DIRECTED, DETAILS}, TrspWithPointsFlags(CHAR_SIDE)},
    {"pgr_trsp_withPoints",
     {ArgKind::EDGES_SQL, ArgKind::RESTRICTIONS_SQL, ArgKind::POINTS_SQL, ArgKind::START_VID, ArgKind::END_VID},
     {DIRECTED, DETAILS}, TrspWithPointsFlags(SIDE_OF_DIRECTED)},
    {"pgr_trsp_withPoints",
     {ArgKind::EDGES_SQL, ArgKind::RESTRICTIONS_SQL, ArgKind::POINTS_SQL, ArgKind::START_VID, ArgKind::END_VIDS},
     {DIRECTED, DETAILS}, TrspWithPointsFlags(SIDE_OF_DIRECTED)},
    {"pgr_trsp_withPoints",
     {ArgKind::EDGES_SQL, ArgKind::RESTRICTIONS_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS, ArgKind::END_VID},
     {DIRECTED, DETAILS}, TrspWithPointsFlags(SIDE_OF_DIRECTED)},
    {"pgr_trsp_withPoints",
     {ArgKind::EDGES_SQL, ArgKind::RESTRICTIONS_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS},
     {DIRECTED, DETAILS}, TrspWithPointsFlags(SIDE_OF_DIRECTED)},
    {"pgr_trsp_withPoints",
     {ArgKind::EDGES_SQL, ArgKind::RESTRICTIONS_SQL, ArgKind::POINTS_SQL, ArgKind::COMBINATIONS_SQL},
     {DIRECTED, DETAILS}, TrspWithPointsFlags(SIDE_OF_DIRECTED)},
};

} // namespace duckdb_pgrouting
