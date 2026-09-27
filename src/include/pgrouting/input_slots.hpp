// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// The columns of _pgr_exec's single input row. Each column is a slot, declared once in
// input_slots.cpp: a row list (a LIST(STRUCT) cell holding one materialized input query) or an id
// list (a LIST(BIGINT) cell). The bind-time type checks, the row-shape capture and the run-time
// registration are one loop each over that declaration.

#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"

#include "pgrouting/driver_request.hpp"
#include "pgrouting/input_registry.hpp"

namespace duckdb {

// The child names and types of one LIST(STRUCT) input column, read off the bound type. `known`
// stays false for a column that is absent or SQLNULL-typed, i.e. one that carries no row shape.
struct RowSchema {
	bool known = false;
	vector<string> names;
	vector<LogicalType> types;
};

// Where one slot sits in this call's input row, and its row shape when it is a row list.
struct BoundSlot {
	idx_t column = DConstants::INVALID_INDEX;
	RowSchema schema;
};

// One entry per declared slot, in declaration order.
using BoundSlots = vector<BoundSlot>;

// Finds every slot's column and checks its type. Throws InvalidInputException naming the column
// when a required slot is missing or a slot has the wrong type.
BoundSlots BindInputSlots(TableFunctionBindInput &input);

// Registers every row list under the SQL text the driver will look it up by, and copies every id
// list into the request.
void MaterializeInputSlots(ClientContext &context, const BoundSlots &slots, DataChunk &input,
                           duckdb_pgrouting::DriverRequest &request, duckdb_pgrouting::InputRegistry &registry);

} // namespace duckdb
