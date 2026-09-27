// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of `sql/withPoints/*.sql`.
//
// Every public pgr_withPoints* function calls _pgr_withPoints_v4, whose C entry
// (src/withPoints/withPoints.c) passes which = 1 for the array and the combinations forms alike,
// and hardcodes n_goals = 0, global = true whatever the SQL wrapper passes.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

namespace {

// The flags a pgr_withPoints* wrapper passes (see the file-top comment). details = true is the
// fallback for overloads that have no details parameter: the Cost and CostMatrix wrappers pass a
// constant true.
DriverFlags WithPointsFlags(bool only_cost, bool normal, DrivingSideSource side, Projection projection) {
	DriverFlags flags;
	flags.only_cost = only_cost;
	flags.normal = normal;
	flags.global = true;
	flags.driving_side = side;
	flags.details = true;
	flags.which = 1;
	flags.projection = projection;
	return flags;
}

constexpr auto CHAR_SIDE = DrivingSideSource::ARGUMENT;
constexpr auto SIDE_OF_DIRECTED = DrivingSideSource::FROM_DIRECTED;

} // namespace

const duckdb::vector<FunctionSpec> WITH_POINTS_SPECS = {
    // pgr_withPoints: details defaults to false. normal = false on many-to-one and many-to-many,
    // unlike pgr_dijkstra and pgr_withPointsCost (which pass it on many-to-one only).
    {"pgr_withPoints",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VID, ArgKind::END_VID, ArgKind::DRIVING_SIDE},
     {DIRECTED, DETAILS}, WithPointsFlags(false, true, CHAR_SIDE, Projection::ALL)},
    {"pgr_withPoints",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VID, ArgKind::END_VIDS, ArgKind::DRIVING_SIDE},
     {DIRECTED, DETAILS}, WithPointsFlags(false, true, CHAR_SIDE, Projection::ALL)},
    {"pgr_withPoints",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS, ArgKind::END_VID, ArgKind::DRIVING_SIDE},
     {DIRECTED, DETAILS}, WithPointsFlags(false, false, CHAR_SIDE, Projection::ALL)},
    {"pgr_withPoints",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS, ArgKind::DRIVING_SIDE},
     {DIRECTED, DETAILS}, WithPointsFlags(false, false, CHAR_SIDE, Projection::ALL)},
    {"pgr_withPoints",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::COMBINATIONS_SQL, ArgKind::DRIVING_SIDE},
     {DIRECTED, DETAILS}, WithPointsFlags(false, true, CHAR_SIDE, Projection::ALL)},
    {"pgr_withPoints", {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VID, ArgKind::END_VID},
     {DIRECTED, DETAILS}, WithPointsFlags(false, true, SIDE_OF_DIRECTED, Projection::ALL)},
    {"pgr_withPoints", {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VID, ArgKind::END_VIDS},
     {DIRECTED, DETAILS}, WithPointsFlags(false, true, SIDE_OF_DIRECTED, Projection::ALL)},
    {"pgr_withPoints", {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS, ArgKind::END_VID},
     {DIRECTED, DETAILS}, WithPointsFlags(false, false, SIDE_OF_DIRECTED, Projection::ALL)},
    {"pgr_withPoints", {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS},
     {DIRECTED, DETAILS}, WithPointsFlags(false, false, SIDE_OF_DIRECTED, Projection::ALL)},
    {"pgr_withPoints", {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::COMBINATIONS_SQL},
     {DIRECTED, DETAILS}, WithPointsFlags(false, true, SIDE_OF_DIRECTED, Projection::ALL)},

    // pgr_withPointsCost: only_cost, a constant details = true, no details parameter; normal =
    // false only on many-to-one (withPointsCost.sql), unlike pgr_dijkstraCost.
    {"pgr_withPointsCost",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VID, ArgKind::END_VID, ArgKind::DRIVING_SIDE},
     {DIRECTED}, WithPointsFlags(true, true, CHAR_SIDE, Projection::COST)},
    {"pgr_withPointsCost",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VID, ArgKind::END_VIDS, ArgKind::DRIVING_SIDE},
     {DIRECTED}, WithPointsFlags(true, true, CHAR_SIDE, Projection::COST)},
    {"pgr_withPointsCost",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS, ArgKind::END_VID, ArgKind::DRIVING_SIDE},
     {DIRECTED}, WithPointsFlags(true, false, CHAR_SIDE, Projection::COST)},
    {"pgr_withPointsCost",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS, ArgKind::DRIVING_SIDE},
     {DIRECTED}, WithPointsFlags(true, true, CHAR_SIDE, Projection::COST)},
    {"pgr_withPointsCost",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::COMBINATIONS_SQL, ArgKind::DRIVING_SIDE},
     {DIRECTED}, WithPointsFlags(true, true, CHAR_SIDE, Projection::COST)},
    {"pgr_withPointsCost", {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VID, ArgKind::END_VID},
     {DIRECTED}, WithPointsFlags(true, true, SIDE_OF_DIRECTED, Projection::COST)},
    {"pgr_withPointsCost", {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VID, ArgKind::END_VIDS},
     {DIRECTED}, WithPointsFlags(true, true, SIDE_OF_DIRECTED, Projection::COST)},
    {"pgr_withPointsCost", {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS, ArgKind::END_VID},
     {DIRECTED}, WithPointsFlags(true, false, SIDE_OF_DIRECTED, Projection::COST)},
    {"pgr_withPointsCost", {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS},
     {DIRECTED}, WithPointsFlags(true, true, SIDE_OF_DIRECTED, Projection::COST)},
    {"pgr_withPointsCost", {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::COMBINATIONS_SQL},
     {DIRECTED}, WithPointsFlags(true, true, SIDE_OF_DIRECTED, Projection::COST)},
    // pgr_withPointsCostMatrix: starts without ends, as pgr_dijkstraCostMatrix.
    {"pgr_withPointsCostMatrix",
     {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS, ArgKind::DRIVING_SIDE},
     {DIRECTED}, WithPointsFlags(true, true, CHAR_SIDE, Projection::COST)},
    {"pgr_withPointsCostMatrix", {ArgKind::EDGES_SQL, ArgKind::POINTS_SQL, ArgKind::START_VIDS},
     {DIRECTED}, WithPointsFlags(true, true, SIDE_OF_DIRECTED, Projection::COST)},
};

} // namespace duckdb_pgrouting
