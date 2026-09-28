// SPDX-License-Identifier: GPL-2.0-or-later

#include "pgrouting/result_emitters.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/types/vector.hpp"

#include "c_types/ii_t_rt.h"
#include "c_types/mst_rt.h"
#include "c_types/path_rt.h"
#include "c_types/routes_t.h"

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
	}
	state.offset += n;
}

} // namespace duckdb
