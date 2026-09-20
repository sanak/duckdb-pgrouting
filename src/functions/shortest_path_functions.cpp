// SPDX-License-Identifier: GPL-2.0-or-later

// The public shortest-path functions. Each one is a bind_replace-only table function: it rewrites
// its call into a query over _pgr_shortestpath_exec, so that the user's edge query is executed by
// DuckDB itself and handed to pgRouting already materialized.

#include "routing/register.hpp"

#include "duckdb/common/exception.hpp"
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

// (SELECT list(t) FROM (<sql>) t)
unique_ptr<ParsedExpression> ListOfRows(ClientContext &context, const string &sql) {
	auto node = make_uniq<SelectNode>();
	vector<unique_ptr<ParsedExpression>> args;
	args.push_back(make_uniq<ColumnRefExpression>(Identifier("t")));
	node->select_list.push_back(make_uniq<FunctionExpression>(Identifier("list"), std::move(args)));
	node->from_table = make_uniq<SubqueryRef>(ParseSingleSelect(context, sql), Identifier("t"));
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

bool NamedFlagOr(TableFunctionBindInput &input, const char *name, bool fallback) {
	auto it = input.named_parameters.find(name);
	if (it == input.named_parameters.end() || it->second.IsNull()) {
		return fallback;
	}
	return BooleanValue::Get(it->second);
}

//===--------------------------------------------------------------------===//
// dijkstra(edges_sql, start_vid, end_vid)
//===--------------------------------------------------------------------===//
unique_ptr<TableRef> DijkstraBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	bool null_input = false;
	for (auto &value : input.inputs) {
		null_input = null_input || value.IsNull();
	}
	const auto directed = NamedFlagOr(input, "directed", true);

	// One row carrying every input the driver needs, as a column each.
	auto row = make_uniq<SelectNode>();
	// A SELECT without FROM still needs a table reference.
	row->from_table = make_uniq<EmptyTableRef>();
	string edges_sql;
	if (null_input) {
		row->select_list.push_back(Named(ConstantExpression::Null(), "edges"));
		row->select_list.push_back(Named(ConstantExpression::Null(), "combinations"));
		row->select_list.push_back(Named(EmptyIdList(), "starts"));
		row->select_list.push_back(Named(EmptyIdList(), "ends"));
	} else {
		edges_sql = StringValue::Get(input.inputs[0]);
		row->select_list.push_back(Named(ListOfRows(context, edges_sql), "edges"));
		row->select_list.push_back(Named(ConstantExpression::Null(), "combinations"));
		row->select_list.push_back(Named(IdList(input.inputs[1]), "starts"));
		row->select_list.push_back(Named(IdList(input.inputs[2]), "ends"));
	}

	vector<unique_ptr<ParsedExpression>> args;
	args.push_back(ScalarSubquery(std::move(row))); // the TABLE argument
	args.push_back(Named(Constant(Value(edges_sql)), "edges_sql"));
	args.push_back(Named(Constant(Value("")), "combinations_sql"));
	args.push_back(Named(Constant(Value::BOOLEAN(directed)), "directed"));
	args.push_back(Named(Constant(Value::BOOLEAN(false)), "only_cost"));
	args.push_back(Named(Constant(Value::BOOLEAN(true)), "normal"));
	args.push_back(Named(Constant(Value::BIGINT(0)), "n_goals"));
	args.push_back(Named(Constant(Value::BOOLEAN(false)), "global"));
	args.push_back(Named(Constant(Value::INTEGER(0)), "which"));
	args.push_back(Named(Constant(Value(" ")), "driving_side"));
	args.push_back(Named(Constant(Value::BOOLEAN(true)), "details"));
	args.push_back(Named(Constant(Value::BOOLEAN(null_input)), "null_input"));
	args.push_back(Named(Constant(Value("path")), "result_kind"));

	auto fref = make_uniq<TableFunctionRef>();
	fref->function = make_uniq<FunctionExpression>(Identifier("_pgr_shortestpath_exec"), std::move(args));

	auto outer = make_uniq<SelectNode>();
	for (const auto *column : {"seq", "path_seq", "start_vid", "end_vid", "node", "edge", "cost", "agg_cost"}) {
		outer->select_list.push_back(make_uniq<ColumnRefExpression>(Identifier(column)));
	}
	outer->from_table = std::move(fref);
	return make_uniq<SubqueryRef>(WrapNode(std::move(outer)));
}

} // namespace

void RegisterShortestPathFunctions(ExtensionLoader &loader) {
	TableFunctionSet dijkstra_set("dijkstra");
	TableFunction one_to_one({LogicalType::VARCHAR, LogicalType::BIGINT, LogicalType::BIGINT}, nullptr, nullptr);
	one_to_one.bind_replace = DijkstraBindReplace;
	one_to_one.named_parameters["directed"] = LogicalType::BOOLEAN;
	dijkstra_set.AddFunction(one_to_one);
	loader.RegisterFunction(dijkstra_set);
}

} // namespace duckdb
