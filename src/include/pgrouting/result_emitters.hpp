// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// What _pgr_exec returns for each ResultShape: the columns, and how a driver's rows become them.
// Upstream's C layer computes some columns itself rather than taking them from the driver (seq,
// path_seq); they are computed here, and whatever depends on the previous row is carried across
// output chunks in EmitState.

#include "duckdb.hpp"

#include "pgrouting/exec_common.hpp"

namespace duckdb {

struct EmitState {
	idx_t offset = 0;          // rows already emitted
	int64_t next_path_seq = 1; // PATH: path_seq of the next row
};

// The columns of `shape`, in order.
void ShapeColumns(duckdb_pgrouting::ResultShape shape, vector<LogicalType> &types, vector<string> &names);

// Writes rows [state.offset, state.offset + n) of `result` into `output` and advances `state`. The
// shape is dispatched once per chunk, never per row. n == 0 writes nothing, so a result that never
// ran (a NULL argument) needs no shape of its own.
void EmitRows(const duckdb_pgrouting::DriverResult &result, EmitState &state, idx_t n, DataChunk &output);

} // namespace duckdb
