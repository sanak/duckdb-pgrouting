// SPDX-License-Identifier: GPL-2.0-or-later

// _pgr_exec: the internal table in-out function every spec-driven public overload is rewritten
// into. It receives one row whose columns carry the already-materialized inputs (input_slots.cpp),
// takes the rest of the request as named parameters (request_params.cpp), runs pgRouting's driver
// once, and streams the driver's rows out in the shape that driver returns (result_emitters.cpp).

#include "pgrouting/register.hpp"

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/function_entry.hpp"
#include "duckdb/catalog/catalog_entry/schema_catalog_entry.hpp"
#include "duckdb/catalog/catalog_transaction.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/identifier.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include "pgrouting/exec_common.hpp"
#include "pgrouting/input_registry.hpp"
#include "pgrouting/input_slots.hpp"
#include "pgrouting/request_params.hpp"
#include "pgrouting/result_emitters.hpp"

namespace duckdb {

namespace {

struct ExecBindData : public TableFunctionData {
	duckdb_pgrouting::DriverRequest request;
	bool null_input = false;
	BoundSlots slots;
	// What the driver returns, and therefore which columns this call has.
	duckdb_pgrouting::ResultShape shape = duckdb_pgrouting::ResultShape::PATH;
};

struct ExecState : public LocalTableFunctionState {
	bool ran = false;
	duckdb_pgrouting::InputRegistry registry;
	duckdb_pgrouting::DriverResult result;
	EmitState emit;
};

unique_ptr<FunctionData> ExecBind(ClientContext &, TableFunctionBindInput &input, vector<LogicalType> &return_types,
                                  vector<Identifier> &names) {
	auto data = make_uniq<ExecBindData>();
	ReadRequestParameters(input.named_parameters, data->request);
	const auto &request = data->request;
	if (request.driver != duckdb_pgrouting::DriverKind::SHORTEST_PATH && !request.points_sql.empty()) {
		throw InvalidInputException("_pgr_exec: points_sql is only supported by driver 'shortest_path'");
	}
	auto null_input = input.named_parameters.find("null_input");
	data->null_input = null_input != input.named_parameters.end() && !null_input->second.IsNull() &&
	                   BooleanValue::Get(null_input->second);
	data->slots = BindInputSlots(input);
	data->shape = duckdb_pgrouting::InfoOf(request.driver).shape;
	ShapeColumns(data->shape, return_types, names);
	return std::move(data);
}

unique_ptr<LocalTableFunctionState> ExecInitLocal(ExecutionContext &, TableFunctionInitInput &,
                                                  GlobalTableFunctionState *) {
	return make_uniq<ExecState>();
}

void RunOnce(ClientContext &context, const ExecBindData &bind, ExecState &state, DataChunk &input) {
	if (input.size() != 1) {
		throw InvalidInputException("_pgr_exec expects exactly one input row");
	}
	if (bind.null_input) {
		return;
	}
	auto request = bind.request;
	// The materialized inputs reference this chunk, so nothing here may outlive the driver call.
	state.registry = duckdb_pgrouting::InputRegistry();
	MaterializeInputSlots(context, bind.slots, input, request, state.registry);
	state.result = duckdb_pgrouting::RunDriver(context, state.registry, request);
}

OperatorResultType ExecFunction(ExecutionContext &context, TableFunctionInput &data_p, DataChunk &input,
                                DataChunk &output) {
	auto &bind = data_p.bind_data->Cast<ExecBindData>();
	auto &state = data_p.local_state->Cast<ExecState>();

	if (!state.ran) {
		state.ran = true;
		state.result = duckdb_pgrouting::DriverResult();
		state.emit = EmitState();
		RunOnce(context.client, bind, state, input);
	}

	const auto n = MinValue<idx_t>(STANDARD_VECTOR_SIZE, state.result.count - state.emit.offset);
	EmitRows(state.result, state.emit, n, output);
	output.SetChildCardinality(n);
	if (state.emit.offset < state.result.count) {
		return OperatorResultType::HAVE_MORE_OUTPUT;
	}
	state.ran = false;
	return OperatorResultType::NEED_MORE_INPUT;
}

// Tags this one internal function the same way TagFunctions (spec_functions.cpp) tags
// every public overload, so both are found by `WHERE tags['ext'] = 'pgrouting'`.
void TagExecFunction(ExtensionLoader &loader) {
	auto &db = loader.GetDatabaseInstance();
	auto &catalog = Catalog::GetSystemCatalog(db);
	auto transaction = CatalogTransaction::GetSystemTransaction(db);
	auto &schema = catalog.GetSchema(transaction, Identifier::DefaultSchema());
	auto entry = schema.GetEntry(transaction, CatalogType::TABLE_FUNCTION_ENTRY, Identifier("_pgr_exec"));
	if (!entry) {
		throw InternalException("pgrouting: _pgr_exec was not registered");
	}
	auto &function_entry = entry->Cast<FunctionEntry>();
	function_entry.tags.insert("ext", "pgrouting");
	function_entry.tags.insert("category", "internal");
}

} // namespace

void RegisterExec(ExtensionLoader &loader) {
	TableFunction exec("_pgr_exec", {LogicalType::TABLE}, nullptr, ExecBind);
	exec.init_local = ExecInitLocal;
	exec.in_out_function = ExecFunction;
	RegisterRequestParameters(exec);
	exec.named_parameters["null_input"] = LogicalType::BOOLEAN;
	loader.RegisterFunction(exec);
	TagExecFunction(loader);
}

} // namespace duckdb
