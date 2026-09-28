// SPDX-License-Identifier: GPL-2.0-or-later

// _pgr_exec: the internal table in-out function every spec-driven public overload is rewritten
// into. It receives one row whose columns carry the already-materialized inputs (input_slots.cpp),
// takes the rest of the request as named parameters (request_params.cpp), runs pgRouting's driver
// once, and streams the driver's rows out in the shape that driver returns (result_emitters.cpp).

#include "pgrouting/register.hpp"

#include <cctype>

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/function_entry.hpp"
#include "duckdb/catalog/catalog_entry/schema_catalog_entry.hpp"
#include "duckdb/catalog/catalog_transaction.hpp"
#include "duckdb/common/exception.hpp"
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
	// The driver is never called and the call returns no rows: an argument was NULL (a STRICT
	// function is never entered), or CheckRequest found a request upstream answers with nothing.
	bool no_rows = false;
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

// pgr_throw_error(msg, hint) (src/common/e_report.c) as this extension raises it. An empty hint adds
// no HINT line, as RunDriver does for a driver error whose log is empty.
[[noreturn]] void ThrowCheck(const string &msg, const string &hint) {
	throw InvalidInputException(hint.empty() ? msg : msg + "\nHINT: " + hint);
}

// estimate_drivingSide (src/withPoints/get_new_queries.cpp) as the withPoints C entries call it: r, l or
// b in either case.
void CheckDrivingSide(const duckdb_pgrouting::DriverRequest &request) {
	const auto side = std::tolower(static_cast<unsigned char>(request.driving_side));
	if (side != 'r' && side != 'l' && side != 'b') {
		ThrowCheck("Invalid value of 'driving side'", "Valid value are 'r', 'l', 'b'");
	}
}

// Upstream's C entries check some parameters before they call the driver (RequestCheck), with
// these messages and hints, verbatim. Returns false when the C entry returns no rows without
// calling the driver.
bool CheckRequest(const duckdb_pgrouting::DriverRequest &request) {
	switch (duckdb_pgrouting::InfoOf(request.driver).check) {
	case duckdb_pgrouting::RequestCheck::NONE:
		return true;
	case duckdb_pgrouting::RequestCheck::ASTAR_PARAMETERS:
		if (request.heuristic > 5 || request.heuristic < 0) {
			ThrowCheck("Unknown heuristic", "Valid values: 0~5");
		}
		if (request.factor <= 0) {
			ThrowCheck("Factor value out of range", "Valid values: positive non zero");
		}
		if (request.epsilon < 1) {
			ThrowCheck("Epsilon value out of range", "Valid values: 1 or greater than 1");
		}
		return true;
	case duckdb_pgrouting::RequestCheck::DRIVING_DISTANCE:
		if (request.distance < 0) {
			ThrowCheck("Negative value found on 'distance'", "Must be positive");
		}
		return true;
	case duckdb_pgrouting::RequestCheck::WITH_POINTS_DD:
		CheckDrivingSide(request);
		if (request.distance < 0) {
			ThrowCheck("Negative value found on 'distance'", "Must be positive");
		}
		return true;
	case duckdb_pgrouting::RequestCheck::SPANNING_TREE:
		if (request.mst_suffix == "DD" && request.distance < 0) {
			ThrowCheck("Negative value found on 'distance'", "Must be positive");
		}
		if ((request.mst_suffix == "BFS" || request.mst_suffix == "DFS") && request.max_depth < 0) {
			ThrowCheck("Negative value found on 'max_depth'", "Must be positive");
		}
		return true;
	case duckdb_pgrouting::RequestCheck::TRAVERSAL:
		if (request.max_depth < 0) {
			ThrowCheck("Negative value found on 'max_depth'", "");
		}
		return true;
	case duckdb_pgrouting::RequestCheck::KSP_K:
		return request.k >= 0;
	case duckdb_pgrouting::RequestCheck::WITH_POINTS_KSP:
		CheckDrivingSide(request);
		if (request.k < 0) {
			ThrowCheck("Invalid value of 'K'", "Valid value are greater than 0");
		}
		return true;
	case duckdb_pgrouting::RequestCheck::DRIVING_SIDE:
		CheckDrivingSide(request);
		return true;
	}
	throw InternalException("pgrouting: unhandled RequestCheck");
}

unique_ptr<FunctionData> ExecBind(ClientContext &, TableFunctionBindInput &input, vector<LogicalType> &return_types,
                                  vector<string> &names) {
	auto data = make_uniq<ExecBindData>();
	ReadRequestParameters(input.named_parameters, data->request);
	const auto &request = data->request;
	if (!duckdb_pgrouting::InfoOf(request.driver).takes_points && !request.points_sql.empty()) {
		throw InvalidInputException("_pgr_exec: driver '%s' takes no points_sql",
		                            duckdb_pgrouting::InfoOf(request.driver).name);
	}
	auto null_input = input.named_parameters.find("null_input");
	data->no_rows = null_input != input.named_parameters.end() && !null_input->second.IsNull() &&
	                BooleanValue::Get(null_input->second);
	data->slots = BindInputSlots(input);
	if (!data->no_rows && !CheckRequest(request)) {
		data->no_rows = true;
	}
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
	if (bind.no_rows) {
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
	output.SetCardinality(n);
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
	auto &schema = catalog.GetSchema(transaction, DEFAULT_SCHEMA);
	auto entry = schema.GetEntry(transaction, CatalogType::TABLE_FUNCTION_ENTRY, "_pgr_exec");
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
