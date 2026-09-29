// SPDX-License-Identifier: GPL-2.0-or-later

#include "pgrouting/result_emitters.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/types/vector.hpp"

#include "c_types/circuits_rt.h"
#include "c_types/edge_rt.h"
#include "c_types/flow_t.h"
#include "c_types/ii_t_rt.h"
#include "c_types/iid_t_rt.h"
#include "c_types/line_graph_full_rt.h"
#include "c_types/transitiveClosure_rt.h"
#include "c_types/mst_rt.h"
#include "c_types/path_rt.h"
#include "c_types/routes_t.h"
#include "c_types/stoerWagner_t.h"
#include "c_types/tsp_tour_rt.h"

namespace duckdb {

namespace {

using duckdb_pgrouting::DriverResult;
using duckdb_pgrouting::ResultShape;

// Upstream's path C entries (src/dijkstra/dijkstra.c and every family modelled on it) number the
// rows from 1 and restart path_seq at 1 after a row whose edge is negative, the last row of a path.
void EmitPath(const DriverResult &result, EmitState &state, idx_t n, DataChunk &output) {
	const auto *rows = result.Rows<Path_rt>();
	auto seq = FlatVector::GetData<int32_t>(output.data[0]);
	auto path_seq = FlatVector::GetData<int32_t>(output.data[1]);
	auto start_vid = FlatVector::GetData<int64_t>(output.data[2]);
	auto end_vid = FlatVector::GetData<int64_t>(output.data[3]);
	auto node = FlatVector::GetData<int64_t>(output.data[4]);
	auto edge = FlatVector::GetData<int64_t>(output.data[5]);
	auto cost = FlatVector::GetData<double>(output.data[6]);
	auto agg_cost = FlatVector::GetData<double>(output.data[7]);
	for (idx_t i = 0; i < n; i++) {
		const auto k = state.offset + i;
		const auto &row = rows[k];
		seq[i] = NumericCast<int32_t>(k + 1);
		path_seq[i] = NumericCast<int32_t>(state.next_path_seq);
		start_vid[i] = row.start_id;
		end_vid[i] = row.end_id;
		node[i] = row.node;
		edge[i] = row.edge;
		cost[i] = row.cost;
		agg_cost[i] = row.agg_cost;
		state.next_path_seq = row.edge < 0 ? 1 : state.next_path_seq + 1;
	}
}

// Upstream's _pgr_connectedComponents emits (call counter + 1, d2.value, d1.id) per pair; so does
// this. The driver has already sorted the pairs by component, then by node.
void EmitPairs(const DriverResult &result, EmitState &state, idx_t n, DataChunk &output) {
	const auto *pairs = result.Rows<II_t_rt>();
	auto seq = FlatVector::GetData<int64_t>(output.data[0]);
	auto component = FlatVector::GetData<int64_t>(output.data[1]);
	auto node = FlatVector::GetData<int64_t>(output.data[2]);
	for (idx_t i = 0; i < n; i++) {
		const auto k = state.offset + i;
		seq[i] = NumericCast<int64_t>(k + 1);
		component[i] = pairs[k].d2.value;
		node[i] = pairs[k].d1.id;
	}
}

// Upstream's root-based C entries (src/driving_distance/*.c, src/spanningTree/*.c,
// src/breadthFirstSearch/breadthFirstSearch.c, src/traversal/depthFirstSearch.c) number the rows
// from 1 and pass the rest of MST_rt through; every public wrapper selects the columns in this
// order, whatever order its C entry builds the tuple in.
void EmitMst(const DriverResult &result, EmitState &state, idx_t n, DataChunk &output) {
	const auto *rows = result.Rows<MST_rt>();
	auto seq = FlatVector::GetData<int64_t>(output.data[0]);
	auto depth = FlatVector::GetData<int64_t>(output.data[1]);
	auto start_vid = FlatVector::GetData<int64_t>(output.data[2]);
	auto pred = FlatVector::GetData<int64_t>(output.data[3]);
	auto node = FlatVector::GetData<int64_t>(output.data[4]);
	auto edge = FlatVector::GetData<int64_t>(output.data[5]);
	auto cost = FlatVector::GetData<double>(output.data[6]);
	auto agg_cost = FlatVector::GetData<double>(output.data[7]);
	for (idx_t i = 0; i < n; i++) {
		const auto k = state.offset + i;
		const auto &row = rows[k];
		seq[i] = NumericCast<int64_t>(k + 1);
		depth[i] = row.depth;
		start_vid[i] = row.from_v;
		pred[i] = row.pred;
		node[i] = row.node;
		edge[i] = row.edge;
		cost[i] = row.cost;
		agg_cost[i] = row.agg_cost;
	}
}

// Upstream's K-shortest-path C entries (src/ksp/ksp.c, src/ksp/withPoints_ksp.c,
// src/ksp/turnRestrictedPath.c) number the rows from 1 and the paths across the whole answer:
// path_id starts at 1 and grows after each row whose edge is -1, path_seq restarts at 1 after each
// row whose edge is negative. The driver's end_id is the path's own endpoint; so is start_id, for
// pgr_ksp and pgr_withPointsKSP, but for pgr_turnRestrictedPath it is the route index (0, 1, …)
// among the input restrictions instead.
void EmitKsp(const DriverResult &result, EmitState &state, idx_t n, DataChunk &output) {
	const auto *rows = result.Rows<Path_rt>();
	auto seq = FlatVector::GetData<int32_t>(output.data[0]);
	auto path_id = FlatVector::GetData<int32_t>(output.data[1]);
	auto path_seq = FlatVector::GetData<int32_t>(output.data[2]);
	auto start_vid = FlatVector::GetData<int64_t>(output.data[3]);
	auto end_vid = FlatVector::GetData<int64_t>(output.data[4]);
	auto node = FlatVector::GetData<int64_t>(output.data[5]);
	auto edge = FlatVector::GetData<int64_t>(output.data[6]);
	auto cost = FlatVector::GetData<double>(output.data[7]);
	auto agg_cost = FlatVector::GetData<double>(output.data[8]);
	for (idx_t i = 0; i < n; i++) {
		const auto k = state.offset + i;
		const auto &row = rows[k];
		seq[i] = NumericCast<int32_t>(k + 1);
		path_id[i] = NumericCast<int32_t>(state.next_path_id);
		path_seq[i] = NumericCast<int32_t>(state.next_path_seq);
		start_vid[i] = row.start_id;
		end_vid[i] = row.end_id;
		node[i] = row.node;
		edge[i] = row.edge;
		cost[i] = row.cost;
		agg_cost[i] = row.agg_cost;
		state.next_path_id = row.edge == -1 ? state.next_path_id + 1 : state.next_path_id;
		state.next_path_seq = row.edge < 0 ? 1 : state.next_path_seq + 1;
	}
}

// Upstream's Via C entries (src/dijkstra/dijkstraVia.c, src/withPoints/withPointsVia.c, src/trsp/trspVia.c,
// src/trsp/trspVia_withPoints.c) number the rows from 1, pass path_id (one per leg, counted even for a leg with no
// path) through, and add 1 to the driver's zero-based path_seq. The route's last row carries edge -2, as the driver
// writes it.
void EmitRoutes(const DriverResult &result, EmitState &state, idx_t n, DataChunk &output) {
	const auto *rows = result.Rows<Routes_t>();
	auto seq = FlatVector::GetData<int32_t>(output.data[0]);
	auto path_id = FlatVector::GetData<int32_t>(output.data[1]);
	auto path_seq = FlatVector::GetData<int32_t>(output.data[2]);
	auto start_vid = FlatVector::GetData<int64_t>(output.data[3]);
	auto end_vid = FlatVector::GetData<int64_t>(output.data[4]);
	auto node = FlatVector::GetData<int64_t>(output.data[5]);
	auto edge = FlatVector::GetData<int64_t>(output.data[6]);
	auto cost = FlatVector::GetData<double>(output.data[7]);
	auto agg_cost = FlatVector::GetData<double>(output.data[8]);
	auto route_agg_cost = FlatVector::GetData<double>(output.data[9]);
	for (idx_t i = 0; i < n; i++) {
		const auto k = state.offset + i;
		const auto &row = rows[k];
		seq[i] = NumericCast<int32_t>(k + 1);
		path_id[i] = row.path_id;
		path_seq[i] = row.path_seq + 1;
		start_vid[i] = row.start_vid;
		end_vid[i] = row.end_vid;
		node[i] = row.node;
		edge[i] = row.edge;
		cost[i] = row.cost;
		agg_cost[i] = row.agg_cost;
		route_agg_cost[i] = row.route_agg_cost;
	}
}

// Upstream's TSP C entries (src/tsp/TSP.c, src/tsp/euclideanTSP.c) number the rows from 1 and pass
// node, cost and agg_cost through.
void EmitTspTour(const DriverResult &result, EmitState &state, idx_t n, DataChunk &output) {
	const auto *rows = result.Rows<TSP_tour_rt>();
	auto seq = FlatVector::GetData<int32_t>(output.data[0]);
	auto node = FlatVector::GetData<int64_t>(output.data[1]);
	auto cost = FlatVector::GetData<double>(output.data[2]);
	auto agg_cost = FlatVector::GetData<double>(output.data[3]);
	for (idx_t i = 0; i < n; i++) {
		const auto k = state.offset + i;
		seq[i] = NumericCast<int32_t>(k + 1);
		node[i] = rows[k].node;
		cost[i] = rows[k].cost;
		agg_cost[i] = rows[k].agg_cost;
	}
}

// Upstream's int64_t results. The ordering C entries (src/ordering/*.c) number them from 1 as a
// BIGINT; pgr_articulationPoints and pgr_bridges (src/components/*.c) number them too, and their
// wrappers select only the id.
void EmitIds(const DriverResult &result, EmitState &state, idx_t n, DataChunk &output) {
	const auto *rows = result.Rows<int64_t>();
	auto seq = FlatVector::GetData<int64_t>(output.data[0]);
	auto id = FlatVector::GetData<int64_t>(output.data[1]);
	for (idx_t i = 0; i < n; i++) {
		const auto k = state.offset + i;
		seq[i] = NumericCast<int64_t>(k + 1);
		id[i] = rows[k];
	}
}

// II_t_rt as (d1.id, d2.value) with a seq from 1: pgr_makeConnected's C entry emits (seq, start_vid,
// end_vid) in that order, the coloring C entries (src/coloring/*.c) the pair without a seq, and the
// dominator tree's C entry (src/dominator/lengauerTarjanDominatorTree.c) emits (seq, vertex, idom) as
// an INTEGER seq.
void EmitIdValue(const DriverResult &result, EmitState &state, idx_t n, DataChunk &output) {
	const auto *rows = result.Rows<II_t_rt>();
	auto seq = FlatVector::GetData<int64_t>(output.data[0]);
	auto id = FlatVector::GetData<int64_t>(output.data[1]);
	auto value = FlatVector::GetData<int64_t>(output.data[2]);
	for (idx_t i = 0; i < n; i++) {
		const auto k = state.offset + i;
		seq[i] = NumericCast<int64_t>(k + 1);
		id[i] = rows[k].d1.id;
		value[i] = rows[k].d2.value;
	}
}

// IID_t_rt as (from_vid, to_vid, cost): the all-pairs C entries (src/allpairs/*.c) pass the three
// through; the betweenness C entry (src/metrics/betweennessCentrality.c) passes from_vid and cost,
// its to_vid being 0.
void EmitTriples(const DriverResult &result, EmitState &state, idx_t n, DataChunk &output) {
	const auto *rows = result.Rows<IID_t_rt>();
	auto from_vid = FlatVector::GetData<int64_t>(output.data[0]);
	auto to_vid = FlatVector::GetData<int64_t>(output.data[1]);
	auto cost = FlatVector::GetData<double>(output.data[2]);
	for (idx_t i = 0; i < n; i++) {
		const auto k = state.offset + i;
		from_vid[i] = rows[k].from_vid;
		to_vid[i] = rows[k].to_vid;
		cost[i] = rows[k].cost;
	}
}

// Upstream's line-graph C entry (src/lineGraph/lineGraph.c) numbers the rows from 1 and passes
// source, target, cost and reverse_cost through; Edge_rt's id is not emitted.
void EmitEdge(const DriverResult &result, EmitState &state, idx_t n, DataChunk &output) {
	const auto *rows = result.Rows<Edge_rt>();
	auto seq = FlatVector::GetData<int32_t>(output.data[0]);
	auto source = FlatVector::GetData<int64_t>(output.data[1]);
	auto target = FlatVector::GetData<int64_t>(output.data[2]);
	auto cost = FlatVector::GetData<double>(output.data[3]);
	auto reverse_cost = FlatVector::GetData<double>(output.data[4]);
	for (idx_t i = 0; i < n; i++) {
		const auto k = state.offset + i;
		seq[i] = NumericCast<int32_t>(k + 1);
		source[i] = rows[k].source;
		target[i] = rows[k].target;
		cost[i] = rows[k].cost;
		reverse_cost[i] = rows[k].reverse_cost;
	}
}

// src/lineGraph/lineGraphFull.c: the rows numbered from 1, then source, target, cost and edge;
// Line_graph_full_rt's id is not emitted.
void EmitLineGraphFull(const DriverResult &result, EmitState &state, idx_t n, DataChunk &output) {
	const auto *rows = result.Rows<Line_graph_full_rt>();
	auto seq = FlatVector::GetData<int32_t>(output.data[0]);
	auto source = FlatVector::GetData<int64_t>(output.data[1]);
	auto target = FlatVector::GetData<int64_t>(output.data[2]);
	auto cost = FlatVector::GetData<double>(output.data[3]);
	auto edge = FlatVector::GetData<int64_t>(output.data[4]);
	for (idx_t i = 0; i < n; i++) {
		const auto k = state.offset + i;
		seq[i] = NumericCast<int32_t>(k + 1);
		source[i] = rows[k].source;
		target[i] = rows[k].target;
		cost[i] = rows[k].cost;
		edge[i] = rows[k].edge;
	}
}

// src/transitiveClosure/transitiveClosure.c numbers the rows from 1 and builds a BIGINT[] from each
// row's target_array. Every row's list entry points into the chunk's own child vector, so a row and
// its targets always leave in the same chunk.
void EmitTransitiveClosure(const DriverResult &result, EmitState &state, idx_t n, DataChunk &output) {
	const auto *rows = result.Rows<TransitiveClosure_rt>();
	auto seq = FlatVector::GetData<int32_t>(output.data[0]);
	auto vid = FlatVector::GetData<int64_t>(output.data[1]);
	auto &targets = output.data[2];
	auto entries = FlatVector::GetData<list_entry_t>(targets);
	const idx_t base = ListVector::GetListSize(targets);
	idx_t size = base;
	for (idx_t i = 0; i < n; i++) {
		const auto &row = rows[state.offset + i];
		seq[i] = NumericCast<int32_t>(state.offset + i + 1);
		vid[i] = row.vid;
		const auto count = NumericCast<idx_t>(row.target_array_size);
		entries[i] = list_entry_t(size, count);
		size += count;
	}
	ListVector::Reserve(targets, size);
	auto child = FlatVector::GetData<int64_t>(ListVector::GetEntry(targets));
	for (idx_t i = 0; i < n; i++) {
		const auto &row = rows[state.offset + i];
		for (idx_t j = 0; j < entries[i].length; j++) {
			child[entries[i].offset + j] = row.target_array[j];
		}
	}
	ListVector::SetListSize(targets, size);
}

// src/mincut/stoerWagner.c numbers the rows from 1 (StoerWagner_t's own seq is not emitted) and
// passes edge, cost and mincut through.
void EmitStoerWagner(const DriverResult &result, EmitState &state, idx_t n, DataChunk &output) {
	const auto *rows = result.Rows<StoerWagner_t>();
	auto seq = FlatVector::GetData<int32_t>(output.data[0]);
	auto edge = FlatVector::GetData<int64_t>(output.data[1]);
	auto cost = FlatVector::GetData<double>(output.data[2]);
	auto mincut = FlatVector::GetData<double>(output.data[3]);
	for (idx_t i = 0; i < n; i++) {
		const auto k = state.offset + i;
		seq[i] = NumericCast<int32_t>(k + 1);
		edge[i] = rows[k].edge;
		cost[i] = rows[k].cost;
		mincut[i] = rows[k].mincut;
	}
}

// src/circuits/hawickCircuits.c numbers the rows from 1 and passes circuits_rt through: the driver
// numbers the circuits (path_id, from 1) and the rows within each (path_seq, from 0), and closes each
// circuit with a row of edge -1 back at its start.
// src/max_flow/minCostMaxFlow.c numbers the rows from 1 and passes Flow_t through;
// src/max_flow/max_flow.c emits the same rows without cost and agg_cost, which its wrappers never
// select.
void EmitFlow(const DriverResult &result, EmitState &state, idx_t n, DataChunk &output) {
	const auto *rows = result.Rows<Flow_t>();
	auto seq = FlatVector::GetData<int32_t>(output.data[0]);
	auto edge = FlatVector::GetData<int64_t>(output.data[1]);
	auto source = FlatVector::GetData<int64_t>(output.data[2]);
	auto target = FlatVector::GetData<int64_t>(output.data[3]);
	auto flow = FlatVector::GetData<int64_t>(output.data[4]);
	auto residual_capacity = FlatVector::GetData<int64_t>(output.data[5]);
	auto cost = FlatVector::GetData<double>(output.data[6]);
	auto agg_cost = FlatVector::GetData<double>(output.data[7]);
	for (idx_t i = 0; i < n; i++) {
		const auto k = state.offset + i;
		const auto &row = rows[k];
		seq[i] = NumericCast<int32_t>(k + 1);
		edge[i] = row.edge;
		source[i] = row.source;
		target[i] = row.target;
		flow[i] = row.flow;
		residual_capacity[i] = row.residual_capacity;
		cost[i] = row.cost;
		agg_cost[i] = row.agg_cost;
	}
}

void EmitCircuits(const DriverResult &result, EmitState &state, idx_t n, DataChunk &output) {
	const auto *rows = result.Rows<circuits_rt>();
	auto seq = FlatVector::GetData<int32_t>(output.data[0]);
	auto path_id = FlatVector::GetData<int32_t>(output.data[1]);
	auto path_seq = FlatVector::GetData<int32_t>(output.data[2]);
	auto start_vid = FlatVector::GetData<int64_t>(output.data[3]);
	auto end_vid = FlatVector::GetData<int64_t>(output.data[4]);
	auto node = FlatVector::GetData<int64_t>(output.data[5]);
	auto edge = FlatVector::GetData<int64_t>(output.data[6]);
	auto cost = FlatVector::GetData<double>(output.data[7]);
	auto agg_cost = FlatVector::GetData<double>(output.data[8]);
	for (idx_t i = 0; i < n; i++) {
		const auto k = state.offset + i;
		const auto &row = rows[k];
		seq[i] = NumericCast<int32_t>(k + 1);
		path_id[i] = row.circuit_id;
		path_seq[i] = row.circuit_path_seq;
		start_vid[i] = row.start_vid;
		end_vid[i] = row.end_vid;
		node[i] = row.node;
		edge[i] = row.edge;
		cost[i] = row.cost;
		agg_cost[i] = row.agg_cost;
	}
}

} // namespace

void ShapeColumns(ResultShape shape, vector<LogicalType> &types, vector<string> &names) {
	switch (shape) {
	case ResultShape::PATH:
		types = {LogicalType::INTEGER, LogicalType::INTEGER, LogicalType::BIGINT, LogicalType::BIGINT,
		         LogicalType::BIGINT,  LogicalType::BIGINT,  LogicalType::DOUBLE, LogicalType::DOUBLE};
		names = {"seq", "path_seq", "start_vid", "end_vid", "node", "edge", "cost", "agg_cost"};
		return;
	case ResultShape::PAIRS:
		// Upstream's pgr_connectedComponents: all three BIGINT, seq included.
		types = {LogicalType::BIGINT, LogicalType::BIGINT, LogicalType::BIGINT};
		names = {"seq", "component", "node"};
		return;
	case ResultShape::MST:
		// Upstream's public wrappers: seq BIGINT, even where the C entry writes an int32.
		types = {LogicalType::BIGINT, LogicalType::BIGINT, LogicalType::BIGINT, LogicalType::BIGINT,
		         LogicalType::BIGINT, LogicalType::BIGINT, LogicalType::DOUBLE, LogicalType::DOUBLE};
		names = {"seq", "depth", "start_vid", "pred", "node", "edge", "cost", "agg_cost"};
		return;
	case ResultShape::KSP:
		types = {LogicalType::INTEGER, LogicalType::INTEGER, LogicalType::INTEGER,
		         LogicalType::BIGINT,  LogicalType::BIGINT,  LogicalType::BIGINT,
		         LogicalType::BIGINT,  LogicalType::DOUBLE,  LogicalType::DOUBLE};
		names = {"seq", "path_id", "path_seq", "start_vid", "end_vid", "node", "edge", "cost", "agg_cost"};
		return;
	case ResultShape::ROUTES:
		types = {LogicalType::INTEGER, LogicalType::INTEGER, LogicalType::INTEGER, LogicalType::BIGINT,
		         LogicalType::BIGINT,  LogicalType::BIGINT,  LogicalType::BIGINT,  LogicalType::DOUBLE,
		         LogicalType::DOUBLE,  LogicalType::DOUBLE};
		names = {"seq",  "path_id", "path_seq", "start_vid", "end_vid",
		         "node", "edge",    "cost",     "agg_cost",  "route_agg_cost"};
		return;
	case ResultShape::TSP_TOUR:
		types = {LogicalType::INTEGER, LogicalType::BIGINT, LogicalType::DOUBLE, LogicalType::DOUBLE};
		names = {"seq", "node", "cost", "agg_cost"};
		return;
	case ResultShape::IDS:
		types = {LogicalType::BIGINT, LogicalType::BIGINT};
		names = {"seq", "id"};
		return;
	case ResultShape::ID_VALUE:
		types = {LogicalType::BIGINT, LogicalType::BIGINT, LogicalType::BIGINT};
		names = {"seq", "id", "value"};
		return;
	case ResultShape::TRIPLES:
		types = {LogicalType::BIGINT, LogicalType::BIGINT, LogicalType::DOUBLE};
		names = {"from_vid", "to_vid", "cost"};
		return;
	case ResultShape::EDGE:
		types = {LogicalType::INTEGER, LogicalType::BIGINT, LogicalType::BIGINT, LogicalType::DOUBLE,
		         LogicalType::DOUBLE};
		names = {"seq", "source", "target", "cost", "reverse_cost"};
		return;
	case ResultShape::LINE_GRAPH_FULL:
		types = {LogicalType::INTEGER, LogicalType::BIGINT, LogicalType::BIGINT, LogicalType::DOUBLE,
		         LogicalType::BIGINT};
		names = {"seq", "source", "target", "cost", "edge"};
		return;
	case ResultShape::TRANSITIVE_CLOSURE:
		types = {LogicalType::INTEGER, LogicalType::BIGINT, LogicalType::LIST(LogicalType::BIGINT)};
		names = {"seq", "vid", "target_array"};
		return;
	case ResultShape::STOER_WAGNER:
		types = {LogicalType::INTEGER, LogicalType::BIGINT, LogicalType::DOUBLE, LogicalType::DOUBLE};
		names = {"seq", "edge", "cost", "mincut"};
		return;
	case ResultShape::CIRCUITS:
		types = {LogicalType::INTEGER, LogicalType::INTEGER, LogicalType::INTEGER,
		         LogicalType::BIGINT,  LogicalType::BIGINT,  LogicalType::BIGINT,
		         LogicalType::BIGINT,  LogicalType::DOUBLE,  LogicalType::DOUBLE};
		names = {"seq", "path_id", "path_seq", "start_vid", "end_vid", "node", "edge", "cost", "agg_cost"};
		return;
	case ResultShape::FLOW:
		types = {LogicalType::INTEGER, LogicalType::BIGINT, LogicalType::BIGINT, LogicalType::BIGINT,
		         LogicalType::BIGINT,  LogicalType::BIGINT, LogicalType::DOUBLE, LogicalType::DOUBLE};
		names = {"seq", "edge", "source", "target", "flow", "residual_capacity", "cost", "agg_cost"};
		return;
	}
	throw InternalException("pgrouting: unhandled ResultShape");
}

void EmitRows(const DriverResult &result, EmitState &state, idx_t n, DataChunk &output) {
	if (n == 0) {
		return;
	}
	switch (result.shape) {
	case ResultShape::PATH:
		EmitPath(result, state, n, output);
		break;
	case ResultShape::PAIRS:
		EmitPairs(result, state, n, output);
		break;
	case ResultShape::MST:
		EmitMst(result, state, n, output);
		break;
	case ResultShape::KSP:
		EmitKsp(result, state, n, output);
		break;
	case ResultShape::ROUTES:
		EmitRoutes(result, state, n, output);
		break;
	case ResultShape::TSP_TOUR:
		EmitTspTour(result, state, n, output);
		break;
	case ResultShape::IDS:
		EmitIds(result, state, n, output);
		break;
	case ResultShape::ID_VALUE:
		EmitIdValue(result, state, n, output);
		break;
	case ResultShape::TRIPLES:
		EmitTriples(result, state, n, output);
		break;
	case ResultShape::EDGE:
		EmitEdge(result, state, n, output);
		break;
	case ResultShape::LINE_GRAPH_FULL:
		EmitLineGraphFull(result, state, n, output);
		break;
	case ResultShape::TRANSITIVE_CLOSURE:
		EmitTransitiveClosure(result, state, n, output);
		break;
	case ResultShape::STOER_WAGNER:
		EmitStoerWagner(result, state, n, output);
		break;
	case ResultShape::CIRCUITS:
		EmitCircuits(result, state, n, output);
		break;
	case ResultShape::FLOW:
		EmitFlow(result, state, n, output);
		break;
	}
	state.offset += n;
}

} // namespace duckdb
