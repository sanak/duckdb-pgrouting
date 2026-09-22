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
#include "routing/withpoints_keys.hpp"

namespace duckdb {

namespace {

// The child names and types of one LIST(STRUCT) input column, read off the bound type. `known`
// stays false for a column that is absent or SQLNULL-typed, i.e. one that carries no row shape at
// all; see RunOnce for why the shape is needed even when the column's cell is NULL.
struct RowSchema {
	bool known = false;
	vector<string> names;
	vector<LogicalType> types;
};

struct ShortestPathExecBindData : public TableFunctionData {
	duckdb_routing::DriverRequest request;
	bool null_input = false;
	// Column positions of the input row, resolved by name so that new columns can be added
	// without disturbing the ones already bound.
	idx_t edges_column = DConstants::INVALID_INDEX;
	idx_t combinations_column = DConstants::INVALID_INDEX;
	idx_t starts_column = DConstants::INVALID_INDEX;
	idx_t ends_column = DConstants::INVALID_INDEX;
	idx_t points_column = DConstants::INVALID_INDEX;
	idx_t edges_of_points_column = DConstants::INVALID_INDEX;
	idx_t edges_no_points_column = DConstants::INVALID_INDEX;
	RowSchema edges_schema;
	RowSchema combinations_schema;
	RowSchema points_schema;
	RowSchema edges_of_points_schema;
	RowSchema edges_no_points_schema;
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

// Builds a registrable input that has the right columns and no rows. pgRouting's fetchers resolve
// and type-check the column names before reading any row (fetch_column_info), and never index the
// per-column vectors when the row count is zero, so `columns` is deliberately left empty.
duckdb_routing::MaterializedInput EmptyInput(const RowSchema &schema) {
	duckdb_routing::MaterializedInput input;
	input.names = schema.names;
	input.types = schema.types;
	for (auto &type : schema.types) {
		// Unpack casts a DECIMAL child to DOUBLE and reports DOUBLE here; both land in
		// ColumnClass::NUMERIC, so the class a zero-row input reports is the same either way.
		input.classes.push_back(ClassOf(type));
	}
	return input;
}

// `name` is the column ('starts'/'ends') this list came from, needed only to name it in the
// exception below.
vector<int64_t> ReadIdList(DataChunk &input, idx_t column, const char *name, bool &present) {
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
		if (child.IsNull()) {
			// A NULL element inside an otherwise well-typed LIST(BIGINT) is reachable from the
			// public API too (dijkstra(sql, [1, NULL]::BIGINT[], 3)), not only from a direct call.
			// BigIntValue::Get on a NULL Value does not assert -- the Value still carries the
			// BIGINT physical type, only its payload is unset -- so it would silently read
			// whatever bytes happen to sit in that union and use them as a vertex id. PostgreSQL
			// and pgRouting reject a NULL array element outright; match that instead of returning
			// an answer that depends on uninitialized memory.
			throw InvalidInputException("_pgr_shortestpath_exec: column '%s' contains a NULL id", name);
		}
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
// overloads' bind_replace. Unpack and ReadIdList reach ListVector::GetChildMutable /
// StructVector::GetEntries / ListValue::GetChildren / BigIntValue::Get, all of which raise
// InternalException (via D_ASSERT) on a type mismatch -- a class that invalidates the whole
// database instance. Checking each column's shape once, here at bind time, turns a wrong-typed
// argument into an ordinary user-input error instead, before any cell is ever read.
//
// An SQLNULL-typed column is exempt from both checks below: every value in it is NULL by
// construction, and both Unpack and ReadIdList already return early on a NULL cell without
// touching a LIST/STRUCT accessor. This matters because bind_replace itself emits an untyped NULL
// constant (SQLNULL) for 'edges'/'combinations' whenever the calling overload does not use them
// (see the row-building comment in shortest_path_functions.cpp) -- rejecting SQLNULL here would
// break that legitimate call shape, not just a hypothetical direct one.
bool IsAlwaysNull(const LogicalType &type) {
	return type.id() == LogicalTypeId::SQLNULL;
}

void CheckIdListColumn(TableFunctionBindInput &input, idx_t column, const char *name) {
	if (column == DConstants::INVALID_INDEX) {
		return;
	}
	const auto &type = input.input_table_types[column];
	if (IsAlwaysNull(type)) {
		return;
	}
	if (ClassOf(type) != duckdb_routing::ColumnClass::INTEGER_ARRAY) {
		throw InvalidInputException("_pgr_shortestpath_exec: column '%s' must be LIST(BIGINT), got %s", name,
		                            type.ToString());
	}
}

void CheckRowListColumn(TableFunctionBindInput &input, idx_t column, const char *name) {
	if (column == DConstants::INVALID_INDEX) {
		return;
	}
	const auto &type = input.input_table_types[column];
	if (IsAlwaysNull(type)) {
		return;
	}
	if (type.id() != LogicalTypeId::LIST || ListType::GetChildType(type).id() != LogicalTypeId::STRUCT) {
		throw InvalidInputException("_pgr_shortestpath_exec: column '%s' must be LIST(STRUCT), got %s", name,
		                            type.ToString());
	}
}

// Records the row shape of an already-validated LIST(STRUCT) column, spelling the child names
// exactly as Unpack does so that a zero-row input answers FindColumn the same way a populated one
// would. A column that is absent or SQLNULL-typed leaves `known` false.
void CaptureRowSchema(TableFunctionBindInput &input, idx_t column, RowSchema &schema) {
	if (column == DConstants::INVALID_INDEX) {
		return;
	}
	const auto &type = input.input_table_types[column];
	if (IsAlwaysNull(type)) {
		return;
	}
	auto &struct_type = ListType::GetChildType(type);
	for (idx_t c = 0; c < StructType::GetChildCount(struct_type); c++) {
		schema.names.push_back(StructType::GetChildName(struct_type, c).GetIdentifierName());
		schema.types.push_back(StructType::GetChildType(struct_type, c));
	}
	schema.known = true;
}

unique_ptr<FunctionData> ShortestPathExecBind(ClientContext &, TableFunctionBindInput &input,
                                              vector<LogicalType> &return_types, vector<Identifier> &names) {
	auto data = make_uniq<ShortestPathExecBindData>();
	auto &request = data->request;
	request.edges_sql = NamedStringOr(input, "edges_sql", "");
	request.combinations_sql = NamedStringOr(input, "combinations_sql", "");
	request.points_sql = NamedStringOr(input, "points_sql", "");
	request.directed = NamedOr<bool>(input, "directed", true);
	request.only_cost = NamedOr<bool>(input, "only_cost", false);
	request.normal = NamedOr<bool>(input, "normal", true);
	request.n_goals = NamedOr<int64_t>(input, "n_goals", 0);
	request.global = NamedOr<bool>(input, "global", false);
	request.which = NamedOr<int32_t>(input, "which", 0);
	request.details = NamedOr<bool>(input, "details", true);
	const auto driving_side = NamedStringOr(input, "driving_side", " ");
	request.driving_side = driving_side.empty() ? ' ' : driving_side[0];
	const auto driver = NamedStringOr(input, "driver", duckdb_routing::DriverKindName(duckdb_routing::DriverKind::SHORTEST_PATH));
	if (!duckdb_routing::ParseDriverKind(driver, request.driver)) {
		throw InvalidInputException("_pgr_shortestpath_exec: unknown driver '%s'", driver);
	}
	if (request.driver != duckdb_routing::DriverKind::SHORTEST_PATH && !request.points_sql.empty()) {
		throw InvalidInputException("_pgr_shortestpath_exec: points_sql is only supported by driver '%s'",
		                            duckdb_routing::DriverKindName(duckdb_routing::DriverKind::SHORTEST_PATH));
	}
	data->null_input = NamedOr<bool>(input, "null_input", false);

	const auto result_kind = NamedStringOr(input, "result_kind", "path");
	if (result_kind != "path") {
		throw InvalidInputException("_pgr_shortestpath_exec: unsupported result_kind '%s'", result_kind);
	}

	data->edges_column = FindInputColumn(input, "edges");
	data->combinations_column = FindInputColumn(input, "combinations");
	data->starts_column = FindInputColumn(input, "starts");
	data->ends_column = FindInputColumn(input, "ends");
	data->points_column = FindInputColumn(input, "points");
	data->edges_of_points_column = FindInputColumn(input, "edges_of_points");
	data->edges_no_points_column = FindInputColumn(input, "edges_no_points");
	if (data->edges_column == DConstants::INVALID_INDEX) {
		throw InvalidInputException("_pgr_shortestpath_exec: the input table has no 'edges' column");
	}
	CheckRowListColumn(input, data->edges_column, "edges");
	CheckRowListColumn(input, data->combinations_column, "combinations");
	CheckRowListColumn(input, data->points_column, "points");
	CheckRowListColumn(input, data->edges_of_points_column, "edges_of_points");
	CheckRowListColumn(input, data->edges_no_points_column, "edges_no_points");
	CheckIdListColumn(input, data->starts_column, "starts");
	CheckIdListColumn(input, data->ends_column, "ends");
	CaptureRowSchema(input, data->edges_column, data->edges_schema);
	CaptureRowSchema(input, data->combinations_column, data->combinations_schema);
	CaptureRowSchema(input, data->points_column, data->points_schema);
	CaptureRowSchema(input, data->edges_of_points_column, data->edges_of_points_schema);
	CaptureRowSchema(input, data->edges_no_points_column, data->edges_no_points_schema);

	return_types = {LogicalType::INTEGER, LogicalType::INTEGER, LogicalType::BIGINT, LogicalType::BIGINT,
	                LogicalType::BIGINT,  LogicalType::BIGINT,  LogicalType::DOUBLE, LogicalType::DOUBLE};
	names = {"seq", "path_seq", "start_vid", "end_vid", "node", "edge", "cost", "agg_cost"};
	return std::move(data);
}

unique_ptr<LocalTableFunctionState> ShortestPathExecInitLocal(ExecutionContext &, TableFunctionInitInput &,
                                                              GlobalTableFunctionState *) {
	return make_uniq<ShortestPathExecState>();
}

// Registers one LIST(STRUCT) column of the input row under (sql, kind). An input query that
// matches no rows makes `list(<row>)` evaluate to NULL; pgRouting still resolves the SQL string in
// the registry first and then reads zero rows, so the bound row shape is registered with no rows.
// A column that is absent, or has no bound row shape (`known` false), is an input this call does
// not use; the driver never asks for it, so it stays unregistered.
void RegisterRowList(ClientContext &context, duckdb_routing::InputRegistry &registry, DataChunk &input,
                     idx_t column, const RowSchema &schema, const string &sql, const char *kind) {
	if (column == DConstants::INVALID_INDEX) {
		return;
	}
	duckdb_routing::MaterializedInput rows;
	if (Unpack(context, input.data[column], 0, rows)) {
		registry.Register(sql, kind, std::move(rows));
	} else if (schema.known) {
		registry.Register(sql, kind, EmptyInput(schema));
	}
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
	// Offered here even when points_sql is set below (where the driver never reads edges_sql
	// itself); that is a no-op only because the public overloads pass 'edges' as an untyped NULL
	// in that case, so it has no bound row shape and RegisterRowList registers nothing for it.
	RegisterRowList(context, state.registry, input, bind.edges_column, bind.edges_schema, request.edges_sql,
	                duckdb_routing::KIND_EDGES);
	RegisterRowList(context, state.registry, input, bind.combinations_column, bind.combinations_schema,
	                request.combinations_sql, duckdb_routing::KIND_COMBINATIONS);
	// With points given, the driver fetches the points query and two edge queries it derives from
	// edges_sql and points_sql, and never edges_sql itself (the caller passes 'edges' as NULL).
	if (!request.points_sql.empty()) {
		const auto keys = duckdb_routing::WithPointsDerivedKeys(request.edges_sql, request.points_sql);
		RegisterRowList(context, state.registry, input, bind.points_column, bind.points_schema,
		                request.points_sql, duckdb_routing::KIND_POINTS);
		RegisterRowList(context, state.registry, input, bind.edges_of_points_column,
		                bind.edges_of_points_schema, keys.of_points, duckdb_routing::KIND_EDGES);
		RegisterRowList(context, state.registry, input, bind.edges_no_points_column,
		                bind.edges_no_points_schema, keys.no_points, duckdb_routing::KIND_EDGES);
	}
	request.starts = ReadIdList(input, bind.starts_column, "starts", request.has_starts);
	request.ends = ReadIdList(input, bind.ends_column, "ends", request.has_ends);

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
	exec.named_parameters["points_sql"] = LogicalType::VARCHAR;
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
	exec.named_parameters["driver"] = LogicalType::VARCHAR;
	loader.RegisterFunction(exec);
	TagExecFunction(loader);
}

} // namespace duckdb
