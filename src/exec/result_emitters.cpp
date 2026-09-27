// SPDX-License-Identifier: GPL-2.0-or-later

#include "pgrouting/result_emitters.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/identifier.hpp"
#include "duckdb/common/types/vector.hpp"
#include "duckdb/common/vector/flat_vector.hpp"

#include "c_types/ii_t_rt.h"
#include "c_types/path_rt.h"

namespace duckdb {

namespace {

using duckdb_pgrouting::DriverResult;
using duckdb_pgrouting::ResultShape;

// Upstream's path C entries (src/dijkstra/dijkstra.c and every family modelled on it) number the
// rows from 1 and restart path_seq at 1 after a row whose edge is negative, the last row of a path.
void EmitPath(const DriverResult &result, EmitState &state, idx_t n, DataChunk &output) {
	const auto *rows = result.Rows<Path_rt>();
	auto seq = FlatVector::ScatterWriter<int32_t>(output.data[0]);
	auto path_seq = FlatVector::ScatterWriter<int32_t>(output.data[1]);
	auto start_vid = FlatVector::ScatterWriter<int64_t>(output.data[2]);
	auto end_vid = FlatVector::ScatterWriter<int64_t>(output.data[3]);
	auto node = FlatVector::ScatterWriter<int64_t>(output.data[4]);
	auto edge = FlatVector::ScatterWriter<int64_t>(output.data[5]);
	auto cost = FlatVector::ScatterWriter<double>(output.data[6]);
	auto agg_cost = FlatVector::ScatterWriter<double>(output.data[7]);
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
	auto seq = FlatVector::ScatterWriter<int64_t>(output.data[0]);
	auto component = FlatVector::ScatterWriter<int64_t>(output.data[1]);
	auto node = FlatVector::ScatterWriter<int64_t>(output.data[2]);
	for (idx_t i = 0; i < n; i++) {
		const auto k = state.offset + i;
		seq[i] = NumericCast<int64_t>(k + 1);
		component[i] = pairs[k].d2.value;
		node[i] = pairs[k].d1.id;
	}
}

} // namespace

void ShapeColumns(ResultShape shape, vector<LogicalType> &types, vector<Identifier> &names) {
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
	}
	state.offset += n;
}

} // namespace duckdb
