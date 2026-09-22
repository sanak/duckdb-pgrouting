// SPDX-License-Identifier: GPL-2.0-or-later

// The public shortest-path functions. Each one is a bind_replace-only table function: it rewrites
// its call into a query over _pgr_shortestpath_exec, so that the user's edge query is executed by
// DuckDB itself and handed to pgRouting already materialized.
//
// Every overload is declared as data in SHORTEST_PATH_SPECS (shortest_path_specs.cpp): a spec row's
// upstream name and positional argument kinds are registered as a DuckDB TableFunction, and
// ShortestPathBindReplace walks the same spec back at call time to build the row that
// _pgr_shortestpath_exec expects.

#include "routing/register.hpp"

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/function_entry.hpp"
#include "duckdb/catalog/catalog_entry/schema_catalog_entry.hpp"
#include "duckdb/catalog/catalog_transaction.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/identifier.hpp"
#include "duckdb/common/unordered_map.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/parser/expression/cast_expression.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/expression/subquery_expression.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/query_node/select_node.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/tableref/emptytableref.hpp"
#include "duckdb/parser/tableref/subqueryref.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"

#include "function_spec.hpp"

namespace duckdb {

namespace {

//===--------------------------------------------------------------------===//
// AST helpers
//===--------------------------------------------------------------------===//
// The replacement is built as a parsed AST, never by string concatenation: the user's SQL is
// parsed once, as its own statement, and can therefore not escape into the generated query.

unique_ptr<SelectStatement> ParseSingleSelect(ClientContext &context, const string &sql) {
	Parser parser(context.GetParserOptions());
	parser.ParseQuery(sql);
	if (parser.statements.size() != 1 || parser.statements[0]->type != StatementType::SELECT_STATEMENT) {
		throw InvalidInputException("Expected a single SELECT statement: %s", sql);
	}
	return unique_ptr_cast<SQLStatement, SelectStatement>(std::move(parser.statements[0]));
}

unique_ptr<SelectStatement> WrapNode(unique_ptr<SelectNode> node) {
	auto stmt = make_uniq<SelectStatement>();
	stmt->node = std::move(node);
	return stmt;
}

unique_ptr<SubqueryExpression> ScalarSubquery(unique_ptr<SelectNode> node) {
	auto sub = make_uniq<SubqueryExpression>();
	sub->GetSubqueryTypeMutable() = SubqueryType::SCALAR;
	sub->SubqueryMutable() = WrapNode(std::move(node));
	return sub;
}

// (SELECT list(_pgr_row) FROM (<sql>) _pgr_row)
//
// The subquery alias must not collide with a column name the user's SQL can produce: DuckDB only
// falls back to struct_pack (giving `list(...)` a LIST(STRUCT) to work with) when the alias does
// not itself resolve as a column reference. A short, generic alias like `t` collides with any user
// query that happens to select a column named `t` (e.g. `SELECT id AS t, ...`): `list(t)` then
// returns a list of that column's own type instead of struct-packing the whole row. Unless that
// column is itself a STRUCT, this fails loudly (CheckRowListColumn rejects the resulting
// `edges`/`combinations` argument as not LIST(STRUCT)) but with a message about internals rather
// than about the real cause, an alias collision. The "_pgr_" prefix makes an accidental collision
// with a real column name unlikely.
unique_ptr<ParsedExpression> ListOfRows(ClientContext &context, const string &sql) {
	static constexpr const char *ROW_ALIAS = "_pgr_row";
	auto node = make_uniq<SelectNode>();
	vector<unique_ptr<ParsedExpression>> args;
	args.push_back(make_uniq<ColumnRefExpression>(Identifier(ROW_ALIAS)));
	node->select_list.push_back(make_uniq<FunctionExpression>(Identifier("list"), std::move(args)));
	node->from_table = make_uniq<SubqueryRef>(ParseSingleSelect(context, sql), Identifier(ROW_ALIAS));
	return ScalarSubquery(std::move(node));
}

// A named table-function argument is encoded as an aliased expression.
unique_ptr<ParsedExpression> Named(unique_ptr<ParsedExpression> expr, const char *name) {
	expr->SetAlias(Identifier(name));
	return expr;
}

unique_ptr<ParsedExpression> Constant(Value value) {
	return ConstantExpression::FromValue(std::move(value));
}

unique_ptr<ParsedExpression> IdList(const Value &id) {
	return Constant(Value::LIST(LogicalType::BIGINT, {id}));
}

unique_ptr<ParsedExpression> EmptyIdList() {
	return Constant(Value::LIST(LogicalType::BIGINT, {}));
}

// START_VIDS / END_VIDS arrive as a Value the caller wrote as e.g. ARRAY[10, 17]; casting it
// explicitly to LIST(BIGINT) means the exec function never has to deal with any other list child
// type.
unique_ptr<ParsedExpression> IdListCast(const Value &value) {
	return make_uniq<CastExpression>(LogicalType::LIST(LogicalType::BIGINT), Constant(value));
}

//===--------------------------------------------------------------------===//
// The spec index a bound function carries, so bind_replace knows which overload it serves.
//===--------------------------------------------------------------------===//

struct ShortestPathFunctionInfo : public TableFunctionInfo {
	explicit ShortestPathFunctionInfo(idx_t spec_index) : spec_index(spec_index) {
	}
	idx_t spec_index;
};

LogicalType TypeOf(duckdb_routing::ArgKind kind) {
	using duckdb_routing::ArgKind;
	switch (kind) {
	case ArgKind::EDGES_SQL:
	case ArgKind::COMBINATIONS_SQL:
		return LogicalType::VARCHAR;
	case ArgKind::START_VID:
	case ArgKind::END_VID:
		return LogicalType::BIGINT;
	case ArgKind::START_VIDS:
	case ArgKind::END_VIDS:
		return LogicalType::LIST(LogicalType::BIGINT);
	}
	throw InternalException("Unhandled ArgKind");
}

LogicalType TypeOf(duckdb_routing::OptionalType type) {
	switch (type) {
	case duckdb_routing::OptionalType::BOOLEAN:
		return LogicalType::BOOLEAN;
	case duckdb_routing::OptionalType::BIGINT:
		return LogicalType::BIGINT;
	}
	throw InternalException("Unhandled OptionalType");
}

// The value of the index-th defaulted parameter: positional when this variant carries it
// positionally (see RegisterShortestPathFunctions), else named, else upstream's default.
Value ResolveOptional(const duckdb_routing::FunctionSpec &spec, TableFunctionBindInput &input, idx_t index) {
	const auto &param = spec.optionals[index];
	const idx_t positional_index = spec.args.size() + index;
	if (positional_index < input.inputs.size()) {
		return input.inputs[positional_index];
	}
	auto it = input.named_parameters.find(param.name);
	if (it != input.named_parameters.end()) {
		return it->second;
	}
	if (param.type == duckdb_routing::OptionalType::BOOLEAN) {
		return Value::BOOLEAN(param.default_value != 0);
	}
	return Value::BIGINT(param.default_value);
}

//===--------------------------------------------------------------------===//
// <public overload>(...) -> _pgr_shortestpath_exec(...)
//===--------------------------------------------------------------------===//
unique_ptr<TableRef> ShortestPathBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	auto &function_info = input.info->Cast<ShortestPathFunctionInfo>();
	const auto &spec = duckdb_routing::SHORTEST_PATH_SPECS[function_info.spec_index];

	bool null_input = false;
	for (auto &value : input.inputs) {
		null_input = null_input || value.IsNull();
	}
	// Upstream declares every overload STRICT, and PostgreSQL applies STRICT whichever way an
	// argument was written, so `directed => NULL` (or `cap => NULL`) yields an empty result there
	// just as a positional NULL does. DuckDB keeps named arguments out of `input.inputs`, so each
	// named defaulted parameter has to be scanned separately for the two to agree.
	for (const auto &param : spec.optionals) {
		auto named = input.named_parameters.find(param.name);
		if (named != input.named_parameters.end() && named->second.IsNull()) {
			null_input = true;
		}
	}

	bool directed = true;
	int64_t n_goals = spec.flags.n_goals;
	bool global = spec.flags.global;
	if (!null_input) {
		for (idx_t i = 0; i < spec.optionals.size(); i++) {
			const auto value = ResolveOptional(spec, input, i);
			const string name = spec.optionals[i].name;
			if (name == "directed") {
				directed = BooleanValue::Get(value);
			} else if (name == "cap") {
				n_goals = BigIntValue::Get(value);
			} else if (name == "global") {
				global = BooleanValue::Get(value);
			} else {
				throw InternalException("routing: unhandled optional parameter '%s'", name);
			}
		}
	}

	string edges_sql;
	string combinations_sql;

	// One row carrying every input the driver needs, as a column each, so the exec function always
	// sees the same four columns. A column this overload does not use gets either an untyped NULL
	// constant ('edges'/'combinations') or an empty but LIST(BIGINT)-typed id list
	// ('starts'/'ends' default to EmptyIdList() below, never a bare NULL). That typing is
	// load-bearing: _pgr_shortestpath_exec's own bind rejects 'starts'/'ends' unless they are
	// SQLNULL or LIST(BIGINT), so emitting an untyped NULL there instead would break every
	// NULL-input call.
	auto row = make_uniq<SelectNode>();
	// A SELECT without FROM still needs a table reference.
	row->from_table = make_uniq<EmptyTableRef>();

	unique_ptr<ParsedExpression> edges_expr = Named(ConstantExpression::Null(), "edges");
	unique_ptr<ParsedExpression> combinations_expr = Named(ConstantExpression::Null(), "combinations");
	unique_ptr<ParsedExpression> starts_expr = Named(EmptyIdList(), "starts");
	unique_ptr<ParsedExpression> ends_expr = Named(EmptyIdList(), "ends");

	if (!null_input) {
		for (idx_t i = 0; i < spec.args.size(); i++) {
			switch (spec.args[i]) {
			case duckdb_routing::ArgKind::EDGES_SQL:
				edges_sql = StringValue::Get(input.inputs[i]);
				edges_expr = Named(ListOfRows(context, edges_sql), "edges");
				break;
			case duckdb_routing::ArgKind::COMBINATIONS_SQL:
				combinations_sql = StringValue::Get(input.inputs[i]);
				combinations_expr = Named(ListOfRows(context, combinations_sql), "combinations");
				break;
			case duckdb_routing::ArgKind::START_VID:
				starts_expr = Named(IdList(input.inputs[i]), "starts");
				break;
			case duckdb_routing::ArgKind::END_VID:
				ends_expr = Named(IdList(input.inputs[i]), "ends");
				break;
			case duckdb_routing::ArgKind::START_VIDS:
				starts_expr = Named(IdListCast(input.inputs[i]), "starts");
				break;
			case duckdb_routing::ArgKind::END_VIDS:
				ends_expr = Named(IdListCast(input.inputs[i]), "ends");
				break;
			}
		}
	}

	row->select_list.push_back(std::move(edges_expr));
	row->select_list.push_back(std::move(combinations_expr));
	row->select_list.push_back(std::move(starts_expr));
	row->select_list.push_back(std::move(ends_expr));

	vector<unique_ptr<ParsedExpression>> args;
	args.push_back(ScalarSubquery(std::move(row))); // the TABLE argument
	args.push_back(Named(Constant(Value(edges_sql)), "edges_sql"));
	args.push_back(Named(Constant(Value(combinations_sql)), "combinations_sql"));
	args.push_back(Named(Constant(Value::BOOLEAN(directed)), "directed"));
	args.push_back(Named(Constant(Value::BOOLEAN(spec.flags.only_cost)), "only_cost"));
	args.push_back(Named(Constant(Value::BOOLEAN(spec.flags.normal)), "normal"));
	args.push_back(Named(Constant(Value::BIGINT(n_goals)), "n_goals"));
	args.push_back(Named(Constant(Value::BOOLEAN(global)), "global"));
	args.push_back(Named(Constant(Value::INTEGER(spec.flags.which)), "which"));
	args.push_back(Named(Constant(Value(string(1, spec.flags.driving_side))), "driving_side"));
	args.push_back(Named(Constant(Value::BOOLEAN(spec.flags.details)), "details"));
	args.push_back(Named(Constant(Value::BOOLEAN(null_input)), "null_input"));
	args.push_back(Named(Constant(Value(spec.flags.result_kind)), "result_kind"));

	auto fref = make_uniq<TableFunctionRef>();
	fref->function = make_uniq<FunctionExpression>(Identifier("_pgr_shortestpath_exec"), std::move(args));

	auto outer = make_uniq<SelectNode>();
	for (const auto *column : {"seq", "path_seq", "start_vid", "end_vid", "node", "edge", "cost", "agg_cost"}) {
		outer->select_list.push_back(make_uniq<ColumnRefExpression>(Identifier(column)));
	}
	outer->from_table = std::move(fref);
	return make_uniq<SubqueryRef>(WrapNode(std::move(outer)));
}

//===--------------------------------------------------------------------===//
// Catalog tags: the single source of the upstream <-> public name mapping.
//===--------------------------------------------------------------------===//
void TagFunctions(ExtensionLoader &loader) {
	auto &db = loader.GetDatabaseInstance();
	auto &catalog = Catalog::GetSystemCatalog(db);
	auto transaction = CatalogTransaction::GetSystemTransaction(db);
	auto &schema = catalog.GetSchema(transaction, Identifier::DefaultSchema());
	for (auto &spec : duckdb_routing::SHORTEST_PATH_SPECS) {
		const auto public_name = duckdb_routing::PublicName(spec.upstream_name);
		auto entry = schema.GetEntry(transaction, CatalogType::TABLE_FUNCTION_ENTRY, Identifier(public_name));
		if (!entry) {
			throw InternalException("routing: function %s was not registered", public_name);
		}
		auto &function_entry = entry->Cast<FunctionEntry>();
		// The tooling reads this back from duckdb_functions() instead of keeping a second copy of
		// the upstream <-> public name mapping in a script.
		function_entry.tags.insert("ext", "routing");
		function_entry.tags.insert("pgrouting_name", spec.upstream_name);
	}
}

} // namespace

void RegisterShortestPathFunctions(ExtensionLoader &loader) {
	unordered_map<string, TableFunctionSet> sets;
	for (idx_t i = 0; i < duckdb_routing::SHORTEST_PATH_SPECS.size(); i++) {
		auto &spec = duckdb_routing::SHORTEST_PATH_SPECS[i];
		const auto public_name = duckdb_routing::PublicName(spec.upstream_name);
		auto entry = sets.find(public_name);
		if (entry == sets.end()) {
			entry = sets.emplace(public_name, TableFunctionSet(Identifier(public_name))).first;
		}
		vector<LogicalType> types;
		for (auto kind : spec.args) {
			types.push_back(TypeOf(kind));
		}
		// PostgreSQL accepts any leading run of an overload's DEFAULT parameters positionally;
		// DuckDB never matches a named parameter positionally. So register one variant per
		// positional prefix length p = 0..k, each accepting all k by name as well.
		for (idx_t p = 0; p <= spec.optionals.size(); p++) {
			auto variant_types = types;
			for (idx_t j = 0; j < p; j++) {
				variant_types.push_back(TypeOf(spec.optionals[j].type));
			}
			TableFunction fn(variant_types, nullptr, nullptr);
			fn.bind_replace = ShortestPathBindReplace;
			for (const auto &param : spec.optionals) {
				fn.named_parameters[param.name] = TypeOf(param.type);
			}
			// The spec index travels in the function's extra_info so bind_replace knows which
			// overload it is serving without re-deriving it from the argument types.
			fn.function_info = make_shared_ptr<ShortestPathFunctionInfo>(i);
			entry->second.AddFunction(fn);
		}
	}
	for (auto &pair : sets) {
		loader.RegisterFunction(pair.second);
	}
	TagFunctions(loader);
}

} // namespace duckdb
