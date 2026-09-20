// SPDX-License-Identifier: GPL-2.0-or-later

// The internal table in-out function every public shortest-path overload is rewritten into.
// It receives one row whose columns carry the already-materialized inputs, runs pgRouting's
// driver once, and streams the driver's tuples out.

#include "routing/register.hpp"

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/function_entry.hpp"
#include "duckdb/catalog/catalog_entry/schema_catalog_entry.hpp"
#include "duckdb/catalog/catalog_transaction.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/identifier.hpp"
#include "duckdb/common/types/vector.hpp"
#include "duckdb/common/vector/flat_vector.hpp"
#include "duckdb/common/vector/list_vector.hpp"
#include "duckdb/common/vector/struct_vector.hpp"
#include "duckdb/common/vector_operations/vector_operations.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include "c_types/path_rt.h"
#include "routing/exec_common.hpp"
#include "routing/input_registry.hpp"

namespace duckdb {

namespace {

struct ShortestPathExecBindData : public TableFunctionData {
	duckdb_routing::DriverRequest request;
	bool null_input = false;
	// Column positions of the input row, resolved by name so that new columns can be added
	// without disturbing the ones already bound.
	idx_t edges_column = DConstants::INVALID_INDEX;
	idx_t combinations_column = DConstants::INVALID_INDEX;
	idx_t starts_column = DConstants::INVALID_INDEX;
	idx_t ends_column = DConstants::INVALID_INDEX;
};

struct ShortestPathExecState : public LocalTableFunctionState {
	bool ran = false;
	duckdb_routing::InputRegistry registry;
	duckdb_routing::DriverResult result;
	idx_t offset = 0;
	int64_t next_path_seq = 1; // carried across output chunks
};

duckdb_routing::ColumnClass ClassOf(const LogicalType &type) {
	using duckdb_routing::ColumnClass;
	switch (type.id()) {
	case LogicalTypeId::TINYINT:
	case LogicalTypeId::SMALLINT:
	case LogicalTypeId::INTEGER:
	case LogicalTypeId::BIGINT:
	case LogicalTypeId::UTINYINT:
	case LogicalTypeId::USMALLINT:
	case LogicalTypeId::UINTEGER:
		// UBIGINT is deliberately absent: it does not round-trip through int64_t.
		return ColumnClass::INTEGER;
	case LogicalTypeId::FLOAT:
	case LogicalTypeId::DOUBLE:
	case LogicalTypeId::DECIMAL:
		return ColumnClass::NUMERIC;
	case LogicalTypeId::VARCHAR:
		return ColumnClass::TEXT;
	case LogicalTypeId::LIST:
		return ListType::GetChildType(type).id() == LogicalTypeId::BIGINT ? ColumnClass::INTEGER_ARRAY
		                                                                  : ColumnClass::UNSUPPORTED;
	default:
		return ColumnClass::UNSUPPORTED;
	}
}

// Unpacks one LIST(STRUCT) cell into per-child vectors. Returns false when the cell is NULL.
bool Unpack(ClientContext &context, Vector &list_column, idx_t row, duckdb_routing::MaterializedInput &out) {
	UnifiedVectorFormat list_format;
	list_column.ToUnifiedFormat(list_format);
	const auto list_idx = list_format.sel->get_index(row);
	if (!list_format.validity.RowIsValid(list_idx)) {
		return false; // NULL input
	}
	const auto entry = UnifiedVectorFormat::GetData<list_entry_t>(list_format)[list_idx];
	auto &child = ListVector::GetChildMutable(list_column);
	const auto child_count = ListVector::GetListSize(list_column);
	auto &struct_children = StructVector::GetEntries(child);
	auto &struct_type = ListType::GetChildType(list_column.GetType());

	out.offset = entry.offset;
	out.count = entry.length;
	out.columns.resize(struct_children.size());
	for (idx_t c = 0; c < struct_children.size(); c++) {
		out.names.push_back(StructType::GetChildName(struct_type, c).GetIdentifierName());
		auto *source = &struct_children[c];
		if (source->GetType().id() == LogicalTypeId::DECIMAL) {
			// pgRouting's ANY-NUMERICAL includes DECIMAL, but reading a DECIMAL cell means
			// knowing its scale and physical width. Cast the whole column once instead.
			auto casted = make_uniq<Vector>(LogicalType::DOUBLE, child_count);
			VectorOperations::Cast(context, *source, *casted, child_count);
			out.owned.push_back(std::move(casted));
			source = out.owned.back().get();
		}
		out.types.push_back(source->GetType());
		out.classes.push_back(ClassOf(source->GetType()));
		source->ToUnifiedFormat(out.columns[c]);
	}
	return true;
}

vector<int64_t> ReadIdList(DataChunk &input, idx_t column, bool &present) {
	vector<int64_t> ids;
	present = false;
	if (column == DConstants::INVALID_INDEX) {
		return ids;
	}
	const auto value = input.GetValue(column, 0);
	if (value.IsNull()) {
		return ids;
	}
	present = true;
	for (auto &child : ListValue::GetChildren(value)) {
		ids.push_back(BigIntValue::Get(child));
	}
	return ids;
}

template <class T>
T NamedOr(TableFunctionBindInput &input, const char *name, T fallback) {
	auto it = input.named_parameters.find(name);
	if (it == input.named_parameters.end() || it->second.IsNull()) {
		return fallback;
	}
	return it->second.GetValue<T>();
}

string NamedStringOr(TableFunctionBindInput &input, const char *name, const string &fallback) {
	auto it = input.named_parameters.find(name);
	if (it == input.named_parameters.end() || it->second.IsNull()) {
		return fallback;
	}
	return StringValue::Get(it->second);
}

idx_t FindInputColumn(TableFunctionBindInput &input, const char *name) {
	for (idx_t i = 0; i < input.input_table_names.size(); i++) {
		if (input.input_table_names[i] == name) {
			return i;
		}
	}
	return DConstants::INVALID_INDEX;
}

// `_pgr_shortestpath_exec` is catalogued and callable by any user, not only through the public
// overloads' bind_replace (which always produces LIST(BIGINT) for 'starts'/'ends'). ReadIdList
// reaches ListValue::GetChildren/BigIntValue::Get, which raise InternalException on a type
// mismatch -- a class that invalidates the whole database instance. Checking the column type once,
// here at bind time, turns a wrong-typed argument into an ordinary user-input error instead.
void CheckIdListColumn(TableFunctionBindInput &input, idx_t column, const char *name) {
	if (column == DConstants::INVALID_INDEX) {
		return;
	}
	const auto &type = input.input_table_types[column];
	if (ClassOf(type) != duckdb_routing::ColumnClass::INTEGER_ARRAY) {
		throw InvalidInputException("_pgr_shortestpath_exec: column '%s' must be LIST(BIGINT), got %s", name,
		                            type.ToString());
	}
}

unique_ptr<FunctionData> ShortestPathExecBind(ClientContext &, TableFunctionBindInput &input,
                                              vector<LogicalType> &return_types, vector<Identifier> &names) {
	auto data = make_uniq<ShortestPathExecBindData>();
	auto &request = data->request;
	request.edges_sql = NamedStringOr(input, "edges_sql", "");
	request.combinations_sql = NamedStringOr(input, "combinations_sql", "");
	request.directed = NamedOr<bool>(input, "directed", true);
	request.only_cost = NamedOr<bool>(input, "only_cost", false);
	request.normal = NamedOr<bool>(input, "normal", true);
	request.n_goals = NamedOr<int64_t>(input, "n_goals", 0);
	request.global = NamedOr<bool>(input, "global", false);
	request.which = NamedOr<int32_t>(input, "which", 0);
	request.details = NamedOr<bool>(input, "details", true);
	const auto driving_side = NamedStringOr(input, "driving_side", " ");
	request.driving_side = driving_side.empty() ? ' ' : driving_side[0];
	data->null_input = NamedOr<bool>(input, "null_input", false);

	const auto result_kind = NamedStringOr(input, "result_kind", "path");
	if (result_kind != "path") {
		throw InvalidInputException("_pgr_shortestpath_exec: unsupported result_kind '%s'", result_kind);
	}

	data->edges_column = FindInputColumn(input, "edges");
	data->combinations_column = FindInputColumn(input, "combinations");
	data->starts_column = FindInputColumn(input, "starts");
	data->ends_column = FindInputColumn(input, "ends");
	if (data->edges_column == DConstants::INVALID_INDEX) {
		throw InvalidInputException("_pgr_shortestpath_exec: the input table has no 'edges' column");
	}
	CheckIdListColumn(input, data->starts_column, "starts");
	CheckIdListColumn(input, data->ends_column, "ends");

	return_types = {LogicalType::INTEGER, LogicalType::INTEGER, LogicalType::BIGINT, LogicalType::BIGINT,
	                LogicalType::BIGINT,  LogicalType::BIGINT,  LogicalType::DOUBLE, LogicalType::DOUBLE};
	names = {"seq", "path_seq", "start_vid", "end_vid", "node", "edge", "cost", "agg_cost"};
	return std::move(data);
}

unique_ptr<LocalTableFunctionState> ShortestPathExecInitLocal(ExecutionContext &, TableFunctionInitInput &,
                                                              GlobalTableFunctionState *) {
	return make_uniq<ShortestPathExecState>();
}

void RunOnce(ClientContext &context, const ShortestPathExecBindData &bind, ShortestPathExecState &state,
             DataChunk &input) {
	if (input.size() != 1) {
		throw InvalidInputException("_pgr_shortestpath_exec expects exactly one input row");
	}
	if (bind.null_input) {
		return;
	}

	auto request = bind.request;
	// The materialized inputs reference this chunk, so nothing here may outlive the driver call.
	state.registry = duckdb_routing::InputRegistry();
	duckdb_routing::MaterializedInput edges;
	if (Unpack(context, input.data[bind.edges_column], 0, edges)) {
		state.registry.Register(request.edges_sql, duckdb_routing::KIND_EDGES, std::move(edges));
	}
	if (bind.combinations_column != DConstants::INVALID_INDEX) {
		duckdb_routing::MaterializedInput combinations;
		if (Unpack(context, input.data[bind.combinations_column], 0, combinations)) {
			state.registry.Register(request.combinations_sql, duckdb_routing::KIND_COMBINATIONS,
			                        std::move(combinations));
		}
	}
	request.starts = ReadIdList(input, bind.starts_column, request.has_starts);
	request.ends = ReadIdList(input, bind.ends_column, request.has_ends);

	state.result = duckdb_routing::RunShortestPath(context, state.registry, request);
}

OperatorResultType ShortestPathExecFunction(ExecutionContext &context, TableFunctionInput &data_p, DataChunk &input,
                                            DataChunk &output) {
	auto &bind = data_p.bind_data->Cast<ShortestPathExecBindData>();
	auto &state = data_p.local_state->Cast<ShortestPathExecState>();

	if (!state.ran) {
		state.ran = true;
		state.result = duckdb_routing::DriverResult();
		state.offset = 0;
		state.next_path_seq = 1;
		RunOnce(context.client, bind, state, input);
	}

	const auto remaining = state.result.count - state.offset;
	const auto n = MinValue<idx_t>(STANDARD_VECTOR_SIZE, remaining);
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
		const auto &row = state.result.rows[k];
		seq[i] = NumericCast<int32_t>(k + 1);
		path_seq[i] = NumericCast<int32_t>(state.next_path_seq);
		start_vid[i] = row.start_id;
		end_vid[i] = row.end_id;
		node[i] = row.node;
		edge[i] = row.edge;
		cost[i] = row.cost;
		agg_cost[i] = row.agg_cost;
		// A negative edge id marks the last row of a path, so the next row starts a new one.
		state.next_path_seq = row.edge < 0 ? 1 : state.next_path_seq + 1;
	}
	output.SetChildCardinality(n);
	state.offset += n;
	if (state.offset < state.result.count) {
		return OperatorResultType::HAVE_MORE_OUTPUT;
	}
	state.ran = false;
	return OperatorResultType::NEED_MORE_INPUT;
}

// Tags this one internal function the same way TagFunctions (shortest_path_functions.cpp) tags
// every public overload, so both are found by `WHERE tags['ext'] = 'routing'`.
void TagExecFunction(ExtensionLoader &loader) {
	auto &db = loader.GetDatabaseInstance();
	auto &catalog = Catalog::GetSystemCatalog(db);
	auto transaction = CatalogTransaction::GetSystemTransaction(db);
	auto &schema = catalog.GetSchema(transaction, Identifier::DefaultSchema());
	auto entry =
	    schema.GetEntry(transaction, CatalogType::TABLE_FUNCTION_ENTRY, Identifier("_pgr_shortestpath_exec"));
	if (!entry) {
		throw InternalException("routing: _pgr_shortestpath_exec was not registered");
	}
	auto &function_entry = entry->Cast<FunctionEntry>();
	function_entry.tags.insert("ext", "routing");
	function_entry.tags.insert("category", "internal");
}

} // namespace

void RegisterShortestPathExec(ExtensionLoader &loader) {
	TableFunction exec("_pgr_shortestpath_exec", {LogicalType::TABLE}, nullptr, ShortestPathExecBind);
	exec.init_local = ShortestPathExecInitLocal;
	exec.in_out_function = ShortestPathExecFunction;
	exec.named_parameters["edges_sql"] = LogicalType::VARCHAR;
	exec.named_parameters["combinations_sql"] = LogicalType::VARCHAR;
	exec.named_parameters["directed"] = LogicalType::BOOLEAN;
	exec.named_parameters["only_cost"] = LogicalType::BOOLEAN;
	exec.named_parameters["normal"] = LogicalType::BOOLEAN;
	exec.named_parameters["n_goals"] = LogicalType::BIGINT;
	exec.named_parameters["global"] = LogicalType::BOOLEAN;
	exec.named_parameters["which"] = LogicalType::INTEGER;
	exec.named_parameters["driving_side"] = LogicalType::VARCHAR;
	exec.named_parameters["details"] = LogicalType::BOOLEAN;
	exec.named_parameters["null_input"] = LogicalType::BOOLEAN;
	exec.named_parameters["result_kind"] = LogicalType::VARCHAR;
	loader.RegisterFunction(exec);
	TagExecFunction(loader);
}

} // namespace duckdb
