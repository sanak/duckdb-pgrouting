// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of sql/max_flow/*.sql. pgr_pushRelabel, pgr_boykovKolmogorov and
// pgr_edmondsKarp call _pgr_maxflow(edges, starts, ends, algorithm) with algorithm 1, 2 or 3, or its
// combinations form, and select `seq, edge_id, source, target, flow, residual_capacity` as `seq,
// edge, start_vid, end_vid, flow, residual_capacity`: the flowed arc's tail and head, not the
// query's sources. A one-vertex argument becomes a one-element array.
// pgr_maxFlow calls the same with algorithm 1 and only_flow, and selects the one row's flow; this driver
// takes only_cost in its place.
// pgr_maxFlowMinCost calls _pgr_maxFlowMinCost(edges, starts, ends, only_cost) and returns its columns as they are;
// pgr_maxFlowMinCost_Cost selects the one only_cost row's cost.
// pgr_edgeDisjointPaths calls _pgr_edgeDisjointPaths(edges, starts, ends, directed) and returns its
// columns, except that its many-to-one wrapper selects `end_vid, start_vid` into `start_vid, end_vid`,
// which is kept.
// pgr_maxCardinalityMatch selects the edge ids of _pgr_maxCardinalityMatch_v4(edges).

#include "function_spec.hpp"

namespace duckdb_pgrouting {

namespace {

DriverFlags MaxFlowFlags(int32_t algorithm) {
	auto flags = ColumnsFlags(DriverKind::MAX_FLOW,
	                          "seq, edge, source AS start_vid, target AS end_vid, flow, residual_capacity");
	flags.algorithm = algorithm;
	return flags;
}

DriverFlags MaxFlowTotalFlags() {
	auto flags = ColumnsFlags(DriverKind::MAX_FLOW, "flow AS pgr_maxflow");
	flags.algorithm = 1;
	flags.only_cost = true;
	return flags;
}

DriverFlags MinCostFlags() {
	return FamilyFlags(DriverKind::MIN_COST_MAX_FLOW, false, Projection::ALL);
}

DriverFlags MinCostTotalFlags() {
	auto flags = ColumnsFlags(DriverKind::MIN_COST_MAX_FLOW, "cost AS pgr_maxflowmincost_cost");
	flags.only_cost = true;
	return flags;
}

DriverFlags EdgeDisjointFlags() {
	return FamilyFlags(DriverKind::EDGE_DISJOINT_PATHS, false, Projection::ALL);
}

// The (TEXT, ANYARRAY, BIGINT) signature of sql/max_flow/edgeDisjointPaths.sql, as upstream writes it.
DriverFlags EdgeDisjointSwappedFlags() {
	return ColumnsFlags(DriverKind::EDGE_DISJOINT_PATHS,
	                    "seq, path_id, path_seq, end_vid AS start_vid, start_vid AS end_vid, node, edge, cost, agg_cost");
}

constexpr int32_t PUSH_RELABEL = 1;
constexpr int32_t BOYKOV_KOLMOGOROV = 2;
constexpr int32_t EDMONDS_KARP = 3;

} // namespace

const duckdb::vector<FunctionSpec> MAX_FLOW_SPECS = {
    {"pgr_pushRelabel", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VID}, {}, MaxFlowFlags(PUSH_RELABEL)},
    {"pgr_pushRelabel", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VIDS}, {}, MaxFlowFlags(PUSH_RELABEL)},
    {"pgr_pushRelabel", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VID}, {}, MaxFlowFlags(PUSH_RELABEL)},
    {"pgr_pushRelabel", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS}, {}, MaxFlowFlags(PUSH_RELABEL)},
    {"pgr_pushRelabel", {ArgKind::EDGES_SQL, ArgKind::COMBINATIONS_SQL}, {}, MaxFlowFlags(PUSH_RELABEL)},

    {"pgr_boykovKolmogorov", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VID}, {},
     MaxFlowFlags(BOYKOV_KOLMOGOROV)},
    {"pgr_boykovKolmogorov", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VIDS}, {},
     MaxFlowFlags(BOYKOV_KOLMOGOROV)},
    {"pgr_boykovKolmogorov", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VID}, {},
     MaxFlowFlags(BOYKOV_KOLMOGOROV)},
    {"pgr_boykovKolmogorov", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS}, {},
     MaxFlowFlags(BOYKOV_KOLMOGOROV)},
    {"pgr_boykovKolmogorov", {ArgKind::EDGES_SQL, ArgKind::COMBINATIONS_SQL}, {}, MaxFlowFlags(BOYKOV_KOLMOGOROV)},

    {"pgr_edmondsKarp", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VID}, {}, MaxFlowFlags(EDMONDS_KARP)},
    {"pgr_edmondsKarp", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VIDS}, {}, MaxFlowFlags(EDMONDS_KARP)},
    {"pgr_edmondsKarp", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VID}, {}, MaxFlowFlags(EDMONDS_KARP)},
    {"pgr_edmondsKarp", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS}, {}, MaxFlowFlags(EDMONDS_KARP)},
    {"pgr_edmondsKarp", {ArgKind::EDGES_SQL, ArgKind::COMBINATIONS_SQL}, {}, MaxFlowFlags(EDMONDS_KARP)},

    // pgr_maxFlow: RETURNS BIGINT, so the column is the function's lower-case name.
    {"pgr_maxFlow", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VID}, {}, MaxFlowTotalFlags(),
     "pgr_maxflow"},
    {"pgr_maxFlow", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VIDS}, {}, MaxFlowTotalFlags(),
     "pgr_maxflow"},
    {"pgr_maxFlow", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VID}, {}, MaxFlowTotalFlags(),
     "pgr_maxflow"},
    {"pgr_maxFlow", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS}, {}, MaxFlowTotalFlags(),
     "pgr_maxflow"},
    {"pgr_maxFlow", {ArgKind::EDGES_SQL, ArgKind::COMBINATIONS_SQL}, {}, MaxFlowTotalFlags(), "pgr_maxflow"},

    {"pgr_maxFlowMinCost", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VID}, {}, MinCostFlags()},
    {"pgr_maxFlowMinCost", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VIDS}, {}, MinCostFlags()},
    {"pgr_maxFlowMinCost", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VID}, {}, MinCostFlags()},
    {"pgr_maxFlowMinCost", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS}, {}, MinCostFlags()},
    {"pgr_maxFlowMinCost", {ArgKind::EDGES_SQL, ArgKind::COMBINATIONS_SQL}, {}, MinCostFlags()},

    // pgr_maxFlowMinCost_Cost: RETURNS FLOAT, so the column is the function's lower-case name.
    {"pgr_maxFlowMinCost_Cost", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VID}, {}, MinCostTotalFlags(),
     "pgr_maxflowmincost_cost"},
    {"pgr_maxFlowMinCost_Cost", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VIDS}, {}, MinCostTotalFlags(),
     "pgr_maxflowmincost_cost"},
    {"pgr_maxFlowMinCost_Cost", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VID}, {}, MinCostTotalFlags(),
     "pgr_maxflowmincost_cost"},
    {"pgr_maxFlowMinCost_Cost", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS}, {}, MinCostTotalFlags(),
     "pgr_maxflowmincost_cost"},
    {"pgr_maxFlowMinCost_Cost", {ArgKind::EDGES_SQL, ArgKind::COMBINATIONS_SQL}, {}, MinCostTotalFlags(),
     "pgr_maxflowmincost_cost"},

    {"pgr_edgeDisjointPaths", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VID}, {DIRECTED},
     EdgeDisjointFlags()},
    {"pgr_edgeDisjointPaths", {ArgKind::EDGES_SQL, ArgKind::START_VID, ArgKind::END_VIDS}, {DIRECTED},
     EdgeDisjointFlags()},
    {"pgr_edgeDisjointPaths", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VID}, {DIRECTED},
     EdgeDisjointSwappedFlags()},
    {"pgr_edgeDisjointPaths", {ArgKind::EDGES_SQL, ArgKind::START_VIDS, ArgKind::END_VIDS}, {DIRECTED},
     EdgeDisjointFlags()},
    {"pgr_edgeDisjointPaths", {ArgKind::EDGES_SQL, ArgKind::COMBINATIONS_SQL}, {DIRECTED}, EdgeDisjointFlags()},

    {"pgr_maxCardinalityMatch", {ArgKind::EDGES_SQL}, {}, ColumnsFlags(DriverKind::MAX_CARDINALITY_MATCH, "id AS edge")},
};

} // namespace duckdb_pgrouting
