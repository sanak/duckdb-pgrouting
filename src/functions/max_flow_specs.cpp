// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of sql/max_flow/*.sql. pgr_pushRelabel, pgr_boykovKolmogorov and
// pgr_edmondsKarp call _pgr_maxflow(edges, starts, ends, algorithm) with algorithm 1, 2 or 3, or its
// combinations form, and select `seq, edge_id, source, target, flow, residual_capacity` as `seq,
// edge, start_vid, end_vid, flow, residual_capacity`: the flowed arc's tail and head, not the
// query's sources. A one-vertex argument becomes a one-element array.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

namespace {

DriverFlags MaxFlowFlags(int32_t algorithm) {
	auto flags = ColumnsFlags(DriverKind::MAX_FLOW,
	                          "seq, edge, source AS start_vid, target AS end_vid, flow, residual_capacity");
	flags.algorithm = algorithm;
	return flags;
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
};

} // namespace duckdb_pgrouting
