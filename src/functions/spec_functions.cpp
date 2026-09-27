// SPDX-License-Identifier: GPL-2.0-or-later

// The spec-driven public functions. Each one is a bind_replace-only table function: it rewrites
// its call into a query over _pgr_exec, so that the user's edge query is executed by DuckDB
// itself and handed to pgRouting already materialized.
//
// Every overload is declared as data in a spec table (function_spec.hpp): a spec row's upstream
// name and positional argument kinds are registered as a DuckDB TableFunction, and
// SpecBindReplace walks the same row back at call time to build the request _pgr_exec runs.

#include "pgrouting/register.hpp"

#include <algorithm>

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/function_entry.hpp"
#include "duckdb/catalog/catalog_entry/schema_catalog_entry.hpp"
#include "duckdb/catalog/catalog_transaction.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/identifier.hpp"
#include "duckdb/common/map.hpp"
#include "duckdb/common/unordered_map.hpp"
#include "duckdb/function/scalar_macro_function.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/parser/common_table_expression_info.hpp"
#include "duckdb/parser/expression/cast_expression.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/parser/expression/comparison_expression.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/expression/operator_expression.hpp"
#include "duckdb/parser/expression/star_expression.hpp"
#include "duckdb/parser/expression/subquery_expression.hpp"
#include "duckdb/parser/parsed_data/create_macro_info.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/parser/parsed_expression_iterator.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/query_node/select_node.hpp"
#include "duckdb/parser/result_modifier.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/tableref/basetableref.hpp"
#include "duckdb/parser/tableref/emptytableref.hpp"
#include "duckdb/parser/tableref/joinref.hpp"
#include "duckdb/parser/tableref/subqueryref.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"

#include "function_spec.hpp"
#include "function_docs.hpp"
#include "pgrouting/input_slots.hpp"
#include "pgrouting/request_params.hpp"
#include "pgrouting/result_emitters.hpp"
#include "sql_template.hpp"

namespace duckdb {

namespace {

using duckdb_pgrouting::ParseSingleSelect;

//===--------------------------------------------------------------------===//
// AST helpers
//===--------------------------------------------------------------------===//
// The replacement is built as a parsed AST, never by string concatenation: the user's SQL is
// parsed once, as its own statement, and can therefore not escape into the generated query.

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
// column is itself a STRUCT, this fails loudly (BindInputSlots, input_slots.cpp, rejects the
// resulting `edges`/`combinations` argument as not LIST(STRUCT)) but with a message about internals rather
// than about the real cause, an alias collision. The "_pgr_" prefix makes an accidental collision
// with a real column name unlikely.
unique_ptr<ParsedExpression> ListOfRows(unique_ptr<SelectStatement> query) {
	static constexpr const char *ROW_ALIAS = "_pgr_row";
	auto node = make_uniq<SelectNode>();
	vector<unique_ptr<ParsedExpression>> args;
	args.push_back(make_uniq<ColumnRefExpression>(Identifier(ROW_ALIAS)));
	node->select_list.push_back(make_uniq<FunctionExpression>(Identifier("list"), std::move(args)));
	node->from_table = make_uniq<SubqueryRef>(std::move(query), Identifier(ROW_ALIAS));
	return ScalarSubquery(std::move(node));
}

unique_ptr<ParsedExpression> ListOfRows(ClientContext &context, const string &sql) {
	return ListOfRows(ParseSingleSelect(context, sql));
}

// AST equivalents of the two queries pgRouting derives from the edges and points SQL when points
// are given (their text, which is only ever a registry key, is WithPointsDerivedKeys in
// withpoints_keys.cpp). The CTE names, `edges.*` and the unqualified `id = edge_id` are upstream's,
// so every name resolves as it does under PostgreSQL: a user query that reads its own table called
// `edges` still reads that table, because a CTE is not visible inside its own body. Each user SQL
// is parsed on its own, once per use, as ListOfRows does; no string concatenation reaches the
// parser.
void AddCTE(ClientContext &context, SelectNode &node, const char *name, const string &sql) {
	auto info = make_uniq<CommonTableExpressionInfo>();
	info->query_node = std::move(ParseSingleSelect(context, sql)->node);
	node.cte_map.map.insert(Identifier(name), std::move(info));
}

unique_ptr<TableRef> NamedTableRef(const char *name) {
	auto ref = make_uniq<BaseTableRef>();
	ref->SetTable(Identifier(name));
	return std::move(ref);
}

unique_ptr<ParsedExpression> IdEqualsEdgeId() {
	return make_uniq<ComparisonExpression>(ExpressionType::COMPARE_EQUAL,
	                                       make_uniq<ColumnRefExpression>(Identifier("id")),
	                                       make_uniq<ColumnRefExpression>(Identifier("edge_id")));
}

// WITH edges AS (<edges_sql>), points AS (<points_sql>)
//   SELECT DISTINCT edges.* FROM edges JOIN points ON (id = edge_id)
unique_ptr<SelectStatement> EdgesOfPoints(ClientContext &context, const string &edges_sql,
                                          const string &points_sql) {
	auto node = make_uniq<SelectNode>();
	AddCTE(context, *node, "edges", edges_sql);
	AddCTE(context, *node, "points", points_sql);
	node->select_list.push_back(make_uniq<StarExpression>(Identifier("edges")));
	auto join = make_uniq<JoinRef>(JoinRefType::REGULAR);
	join->type = JoinType::INNER;
	join->left = NamedTableRef("edges");
	join->right = NamedTableRef("points");
	join->condition = IdEqualsEdgeId();
	node->from_table = std::move(join);
	node->modifiers.push_back(make_uniq<DistinctModifier>());
	return WrapNode(std::move(node));
}

// WITH edges AS (<edges_sql>), points AS (<points_sql>)
//   SELECT edges.* FROM edges WHERE NOT EXISTS (SELECT edge_id FROM points WHERE id = edge_id)
unique_ptr<SelectStatement> EdgesWithoutPoints(ClientContext &context, const string &edges_sql,
                                               const string &points_sql) {
	auto inner = make_uniq<SelectNode>();
	inner->select_list.push_back(make_uniq<ColumnRefExpression>(Identifier("edge_id")));
	inner->from_table = NamedTableRef("points");
	inner->where_clause = IdEqualsEdgeId();
	auto exists = make_uniq<SubqueryExpression>();
	exists->GetSubqueryTypeMutable() = SubqueryType::EXISTS;
	exists->SubqueryMutable() = WrapNode(std::move(inner));

	auto node = make_uniq<SelectNode>();
	AddCTE(context, *node, "edges", edges_sql);
	AddCTE(context, *node, "points", points_sql);
	node->select_list.push_back(make_uniq<StarExpression>(Identifier("edges")));
	node->from_table = NamedTableRef("edges");
	node->where_clause = make_uniq<OperatorExpression>(ExpressionType::OPERATOR_NOT, std::move(exists));
	return WrapNode(std::move(node));
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
// The spec row a bound function carries, so bind_replace knows which overload it serves.
//===--------------------------------------------------------------------===//

struct SpecFunctionInfo : public TableFunctionInfo {
	explicit SpecFunctionInfo(const duckdb_pgrouting::FunctionSpec &spec) : spec(spec) {
	}
	const duckdb_pgrouting::FunctionSpec &spec;
};

// Every spec table (function_spec.hpp). A new family adds its table here.
const vector<const vector<duckdb_pgrouting::FunctionSpec> *> &SpecTables() {
	static const vector<const vector<duckdb_pgrouting::FunctionSpec> *> TABLES = {
	    &duckdb_pgrouting::DIJKSTRA_SPECS,          &duckdb_pgrouting::WITH_POINTS_SPECS,
	    &duckdb_pgrouting::BD_DIJKSTRA_SPECS,       &duckdb_pgrouting::BELLMAN_FORD_SPECS,
	    &duckdb_pgrouting::DAG_SHORTEST_PATH_SPECS, &duckdb_pgrouting::BREADTH_FIRST_SEARCH_SPECS,
	    &duckdb_pgrouting::COMPONENTS_SPECS,        &duckdb_pgrouting::COLORING_SPECS,
	    &duckdb_pgrouting::ASTAR_SPECS,             &duckdb_pgrouting::BD_ASTAR_SPECS,
	    &duckdb_pgrouting::DRIVING_DISTANCE_SPECS,  &duckdb_pgrouting::SPANNING_TREE_SPECS,
	    &duckdb_pgrouting::TRAVERSAL_SPECS,         &duckdb_pgrouting::KSP_SPECS,
	    &duckdb_pgrouting::TRSP_SPECS,              &duckdb_pgrouting::TSP_SPECS,
	    &duckdb_pgrouting::ORDERING_SPECS,          &duckdb_pgrouting::ALLPAIRS_SPECS,
	    &duckdb_pgrouting::METRICS_SPECS,           &duckdb_pgrouting::PLANAR_SPECS,
	    &duckdb_pgrouting::LINE_GRAPH_SPECS,        &duckdb_pgrouting::TRANSITIVE_CLOSURE_SPECS,
	    &duckdb_pgrouting::DOMINATOR_SPECS,         &duckdb_pgrouting::MINCUT_SPECS,
	    &duckdb_pgrouting::CIRCUITS_SPECS,          &duckdb_pgrouting::MAX_FLOW_SPECS,
	    &duckdb_pgrouting::CHINESE_SPECS,           &duckdb_pgrouting::CONTRACTION_SPECS};
	return TABLES;
}

// The outer SELECT list of a public overload over _pgr_exec's columns.
vector<unique_ptr<ParsedExpression>> ProjectionList(const duckdb_pgrouting::FunctionSpec &spec) {
	auto column_refs = [](const vector<Identifier> &names) {
		vector<unique_ptr<ParsedExpression>> list;
		for (auto &name : names) {
			list.push_back(make_uniq<ColumnRefExpression>(name));
		}
		return list;
	};
	switch (spec.flags.projection) {
	case duckdb_pgrouting::Projection::ALL: {
		vector<LogicalType> types;
		vector<Identifier> names;
		ShapeColumns(duckdb_pgrouting::InfoOf(spec.flags.driver).shape, types, names);
		return column_refs(names);
	}
	case duckdb_pgrouting::Projection::COST:
	case duckdb_pgrouting::Projection::COST_OF_PATH:
	case duckdb_pgrouting::Projection::COST_SORTED:
		return column_refs({"start_vid", "end_vid", "agg_cost"});
	case duckdb_pgrouting::Projection::EDGE_COST:
		return column_refs({"edge", "cost"});
	case duckdb_pgrouting::Projection::COLUMNS:
		// Fixed text from a spec table, never the caller's; CheckSpec parsed it at load.
		return Parser::ParseExpressionList(spec.flags.columns);
	}
	throw InternalException("Unhandled Projection");
}

LogicalType TypeOf(duckdb_pgrouting::ArgKind kind) {
	using duckdb_pgrouting::ArgKind;
	switch (kind) {
	case ArgKind::EDGES_SQL:
	case ArgKind::COMBINATIONS_SQL:
	case ArgKind::POINTS_SQL:
	case ArgKind::DRIVING_SIDE:
	case ArgKind::RESTRICTIONS_SQL:
	case ArgKind::MATRIX_SQL:
	case ArgKind::COORDINATES_SQL:
		return LogicalType::VARCHAR;
	case ArgKind::START_VID:
	case ArgKind::END_VID:
	case ArgKind::ROOT:
	case ArgKind::ROOT_VID:
		return LogicalType::BIGINT;
	case ArgKind::START_VIDS:
	case ArgKind::END_VIDS:
	case ArgKind::VIDS:
	case ArgKind::ROOTS:
	case ArgKind::VIA:
		return LogicalType::LIST(LogicalType::BIGINT);
	case ArgKind::DISTANCE:
		return LogicalType::DOUBLE;
	case ArgKind::K:
		return LogicalType::INTEGER;
	}
	throw InternalException("Unhandled ArgKind");
}

LogicalType TypeOf(duckdb_pgrouting::OptionalType type) {
	switch (type) {
	case duckdb_pgrouting::OptionalType::BOOLEAN:
		return LogicalType::BOOLEAN;
	case duckdb_pgrouting::OptionalType::BIGINT:
		return LogicalType::BIGINT;
	case duckdb_pgrouting::OptionalType::INTEGER:
		return LogicalType::INTEGER;
	case duckdb_pgrouting::OptionalType::DOUBLE:
		return LogicalType::DOUBLE;
	case duckdb_pgrouting::OptionalType::INTEGER_LIST:
		return LogicalType::LIST(LogicalType::INTEGER);
	case duckdb_pgrouting::OptionalType::BIGINT_LIST:
		return LogicalType::LIST(LogicalType::BIGINT);
	}
	throw InternalException("Unhandled OptionalType");
}

bool IsListType(duckdb_pgrouting::OptionalType type) {
	return type == duckdb_pgrouting::OptionalType::INTEGER_LIST || type == duckdb_pgrouting::OptionalType::BIGINT_LIST;
}

// The value of the index-th defaulted parameter: positional when this variant carries it
// positionally (see RegisterSpecFunctions), else named, else upstream's default.
Value ResolveOptional(const duckdb_pgrouting::FunctionSpec &spec, TableFunctionBindInput &input, idx_t index) {
	const auto &param = spec.optionals[index];
	const idx_t positional_index = spec.args.size() + index;
	if (positional_index < input.inputs.size()) {
		return input.inputs[positional_index];
	}
	auto it = input.named_parameters.find(param.name);
	if (it != input.named_parameters.end()) {
		return it->second;
	}
	return Value(param.default_value).DefaultCastAs(TypeOf(param.type));
}

// A positional argument's name in a scalar macro. The overloads of one upstream function that take
// the same number of arguments share one macro (DuckDB tells macros apart by argument count only),
// so an argument that is one vertex in one overload and an array in another shares one name. Only the
// argument kinds a one-value upstream function takes are named; CheckSpec rejects a scalar_column row
// with another.
const char *MacroParameterName(duckdb_pgrouting::ArgKind kind) {
	switch (kind) {
	case duckdb_pgrouting::ArgKind::EDGES_SQL:
		return "edges_sql";
	case duckdb_pgrouting::ArgKind::COMBINATIONS_SQL:
		return "combinations_sql";
	case duckdb_pgrouting::ArgKind::START_VID:
	case duckdb_pgrouting::ArgKind::START_VIDS:
		return "start_vids";
	case duckdb_pgrouting::ArgKind::END_VID:
	case duckdb_pgrouting::ArgKind::END_VIDS:
		return "end_vids";
	default:
		return nullptr;
	}
}

// A scalar macro's parameter names, positional ones first, then the defaulted ones.
vector<string> MacroParameterNames(const duckdb_pgrouting::FunctionSpec &spec) {
	vector<string> names;
	for (auto kind : spec.args) {
		names.push_back(MacroParameterName(kind));
	}
	for (const auto &param : spec.optionals) {
		names.push_back(param.name);
	}
	return names;
}

// Every column a COLUMNS select list reads must be one of the shape's own, unqualified.
void CheckColumnRefs(const ParsedExpression &expr, const vector<Identifier> &names, const char *function_name) {
	if (expr.GetExpressionClass() == ExpressionClass::COLUMN_REF) {
		auto &ref = expr.Cast<ColumnRefExpression>();
		// Compared as written: Identifier equality ignores case, and a select list must name the
		// shape's columns exactly.
		const auto &column = ref.GetColumnName().GetIdentifierName();
		const bool known = std::any_of(names.begin(), names.end(), [&](const Identifier &name) {
			return name.GetIdentifierName() == column;
		});
		if (ref.IsQualified() || !known) {
			throw InternalException("pgrouting: %s selects %s, which its driver does not return", function_name,
			                        ref.ToString());
		}
	}
	ParsedExpressionIterator::EnumerateChildren(
	    expr, [&](const ParsedExpression &child) { CheckColumnRefs(child, names, function_name); });
}

// A spec row is data. These are the ways it can disagree with the rest of the extension; checked
// once at load, they fail every test instead of only a call to the one overload.
void CheckSpec(const duckdb_pgrouting::FunctionSpec &spec) {
	for (const auto &param : spec.optionals) {
		// A defaulted array fills an id-list slot of the input row; every other parameter a request
		// parameter.
		const bool known = IsListType(param.type) ? IsIdListSlot(param.request_field)
		                                          : IsRequestParameter(param.request_field);
		if (!known) {
			throw InternalException("pgrouting: %s's parameter %s sets unknown request field %s", spec.upstream_name,
			                        param.name, param.request_field);
		}
	}
	for (const auto &param : spec.optionals) {
		Value default_value(param.default_value);
		if (!default_value.DefaultTryCastAs(TypeOf(param.type))) {
			throw InternalException("pgrouting: %s's parameter %s has default '%s', not a %s", spec.upstream_name,
			                        param.name, param.default_value, TypeOf(param.type).ToString());
		}
	}
	if ((spec.flags.columns != nullptr) != (spec.flags.projection == duckdb_pgrouting::Projection::COLUMNS)) {
		throw InternalException("pgrouting: %s has a column list without Projection::COLUMNS, or the reverse",
		                        spec.upstream_name);
	}
	const auto shape = duckdb_pgrouting::InfoOf(spec.flags.driver).shape;
	switch (spec.flags.projection) {
	case duckdb_pgrouting::Projection::ALL:
		break;
	case duckdb_pgrouting::Projection::COST:
	case duckdb_pgrouting::Projection::COST_OF_PATH:
	case duckdb_pgrouting::Projection::COST_SORTED:
		if (shape != duckdb_pgrouting::ResultShape::PATH) {
			throw InternalException("pgrouting: %s projects path columns out of a driver that returns no paths",
			                        spec.upstream_name);
		}
		break;
	case duckdb_pgrouting::Projection::EDGE_COST:
		if (shape != duckdb_pgrouting::ResultShape::MST) {
			throw InternalException("pgrouting: %s projects tree columns out of a driver that returns no trees",
			                        spec.upstream_name);
		}
		break;
	case duckdb_pgrouting::Projection::COLUMNS: {
		vector<LogicalType> types;
		vector<Identifier> names;
		ShapeColumns(shape, types, names);
		auto list = Parser::ParseExpressionList(spec.flags.columns);
		if (list.empty()) {
			throw InternalException("pgrouting: %s has an empty column list", spec.upstream_name);
		}
		for (auto &expr : list) {
			CheckColumnRefs(*expr, names, spec.upstream_name);
		}
		break;
	}
	}
	if (spec.scalar_column != nullptr) {
		// The macro selects scalar_column from the table function, so the overload must return
		// exactly that one column.
		bool single = spec.flags.projection == duckdb_pgrouting::Projection::COLUMNS;
		if (single) {
			auto list = Parser::ParseExpressionList(spec.flags.columns);
			single = list.size() == 1 && list[0]->GetAlias() == spec.scalar_column;
		}
		if (!single) {
			throw InternalException("pgrouting: %s's scalar column %s is not its one selected column",
			                        spec.upstream_name, spec.scalar_column);
		}
		for (auto kind : spec.args) {
			if (MacroParameterName(kind) == nullptr) {
				throw InternalException("pgrouting: %s's scalar macro has an argument it cannot name",
				                        spec.upstream_name);
			}
		}
	}
}

//===--------------------------------------------------------------------===//
// <public overload>(...) -> _pgr_exec(...)
//===--------------------------------------------------------------------===//
unique_ptr<TableRef> SpecBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	const auto &spec = input.info->Cast<SpecFunctionInfo>().spec;

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

	// The request starts from the flags this overload fixes; the caller's defaulted parameters and
	// required arguments fill in the rest, and every field then reaches _pgr_exec as a named
	// argument (RequestArguments).
	duckdb_pgrouting::DriverRequest request;
	request.only_cost = spec.flags.only_cost;
	request.normal = spec.flags.normal;
	request.n_goals = spec.flags.n_goals;
	request.global = spec.flags.global;
	request.details = spec.flags.details;
	request.which = spec.flags.which;
	request.driver = spec.flags.driver;
	request.mst_suffix = spec.flags.mst_suffix;
	request.algorithm = spec.flags.algorithm;
	// The defaulted arrays (and a fixed methods list) become id-list columns of the input row below.
	vector<unique_ptr<ParsedExpression>> id_list_exprs;
	if (!null_input) {
		for (idx_t i = 0; i < spec.optionals.size(); i++) {
			const auto &param = spec.optionals[i];
			// CheckSpec established at load that request_field names a request parameter, or an
			// id-list slot for a defaulted array.
			if (IsListType(param.type)) {
				id_list_exprs.push_back(Named(IdListCast(ResolveOptional(spec, input, i)), param.request_field));
			} else {
				SetRequestParameter(request, param.request_field, ResolveOptional(spec, input, i));
			}
		}
		if (spec.flags.contraction_method != 0) {
			id_list_exprs.push_back(Named(IdList(Value::BIGINT(spec.flags.contraction_method)), "methods"));
		}
	}
	// A CHAR signature's driving side is its argument (read below). The other withPoints
	// signatures pass (CASE WHEN directed THEN 'r' ELSE 'b' END) upstream, from the directed
	// resolved above.
	if (spec.flags.driving_side == duckdb_pgrouting::DrivingSideSource::FROM_DIRECTED) {
		request.driving_side = request.directed ? 'r' : 'b';
	}

	bool has_points = false;
	// Whether this overload takes an edge query at all: the TSP overloads read a matrix or
	// coordinates query instead, and leave 'edges' an untyped NULL.
	bool has_edges = false;

	// One row carrying every input the driver needs, as a column each, so the exec function always
	// sees at least these six columns (an overload with a defaulted array or a fixed methods list adds
	// that array's slot column, one with restrictions adds 'restrictions', one with a
	// matrix or coordinates query adds 'matrix' or 'coordinates' and leaves 'edges' NULL, and one
	// with points adds 'points', 'edges_of_points' and 'edges_no_points', and 'edges' is then NULL). A
	// column this overload does not use gets either
	// an untyped NULL constant ('edges'/'combinations') or an empty but LIST(BIGINT)-typed id list
	// ('starts'/'ends'/'roots'/'via' default to EmptyIdList() below, never a bare NULL; 'methods' and
	// 'forbidden' are emitted only by overloads that take them). That typing is load-bearing:
	// _pgr_exec's own bind rejects an id-list column unless it is SQLNULL or LIST(BIGINT), so emitting
	// an untyped NULL there instead would break every NULL-input call.
	auto row = make_uniq<SelectNode>();
	// A SELECT without FROM still needs a table reference.
	row->from_table = make_uniq<EmptyTableRef>();

	unique_ptr<ParsedExpression> edges_expr = Named(ConstantExpression::Null(), "edges");
	unique_ptr<ParsedExpression> combinations_expr = Named(ConstantExpression::Null(), "combinations");
	unique_ptr<ParsedExpression> starts_expr = Named(EmptyIdList(), "starts");
	unique_ptr<ParsedExpression> ends_expr = Named(EmptyIdList(), "ends");
	unique_ptr<ParsedExpression> roots_expr =
	    Named(spec.flags.root_zero ? IdList(Value::BIGINT(0)) : EmptyIdList(), "roots");
	unique_ptr<ParsedExpression> via_expr = Named(EmptyIdList(), "via");
	// Only an overload with a restrictions argument emits this one.
	unique_ptr<ParsedExpression> restrictions_expr;
	// Only an overload with a matrix or coordinates argument emits that one.
	unique_ptr<ParsedExpression> matrix_expr;
	unique_ptr<ParsedExpression> coordinates_expr;
	// Only an overload with a points argument emits these three; see the loop below.
	unique_ptr<ParsedExpression> points_expr;
	unique_ptr<ParsedExpression> edges_of_points_expr;
	unique_ptr<ParsedExpression> edges_no_points_expr;

	if (!null_input) {
		for (idx_t i = 0; i < spec.args.size(); i++) {
			switch (spec.args[i]) {
			case duckdb_pgrouting::ArgKind::EDGES_SQL:
				// Materialized after the loop: with points given, the driver never fetches the
				// edge query itself, only the two it derives from it and the points query.
				request.edges_sql = StringValue::Get(input.inputs[i]);
				has_edges = true;
				break;
			case duckdb_pgrouting::ArgKind::COMBINATIONS_SQL:
				request.combinations_sql = StringValue::Get(input.inputs[i]);
				combinations_expr = Named(ListOfRows(context, request.combinations_sql), "combinations");
				break;
			case duckdb_pgrouting::ArgKind::START_VID:
				starts_expr = Named(IdList(input.inputs[i]), "starts");
				break;
			case duckdb_pgrouting::ArgKind::END_VID:
				ends_expr = Named(IdList(input.inputs[i]), "ends");
				break;
			case duckdb_pgrouting::ArgKind::START_VIDS:
				starts_expr = Named(IdListCast(input.inputs[i]), "starts");
				break;
			case duckdb_pgrouting::ArgKind::END_VIDS:
				ends_expr = Named(IdListCast(input.inputs[i]), "ends");
				break;
			case duckdb_pgrouting::ArgKind::VIDS:
				// One array as both starts and ends: the per-family drivers only read the arrays when
				// both are given, and pair every start with every end; a vertex paired with itself
				// yields no row.
				starts_expr = Named(IdListCast(input.inputs[i]), "starts");
				ends_expr = Named(IdListCast(input.inputs[i]), "ends");
				break;
			case duckdb_pgrouting::ArgKind::POINTS_SQL:
				request.points_sql = StringValue::Get(input.inputs[i]);
				has_points = true;
				break;
			case duckdb_pgrouting::ArgKind::DRIVING_SIDE: {
				// Only the first character reaches the driver, as upstream's process layer passes
				// driving_side[0].
				SetRequestParameter(request, "driving_side", input.inputs[i]);
				break;
			}
			case duckdb_pgrouting::ArgKind::ROOT:
				roots_expr = Named(IdList(input.inputs[i]), "roots");
				break;
			case duckdb_pgrouting::ArgKind::ROOTS:
				roots_expr = Named(IdListCast(input.inputs[i]), "roots");
				break;
			case duckdb_pgrouting::ArgKind::DISTANCE:
				SetRequestParameter(request, "distance", input.inputs[i]);
				break;
			case duckdb_pgrouting::ArgKind::K:
				SetRequestParameter(request, "k", input.inputs[i]);
				break;
			case duckdb_pgrouting::ArgKind::VIA:
				via_expr = Named(IdListCast(input.inputs[i]), "via");
				break;
			case duckdb_pgrouting::ArgKind::RESTRICTIONS_SQL:
				request.restrictions_sql = StringValue::Get(input.inputs[i]);
				restrictions_expr = Named(ListOfRows(context, request.restrictions_sql), "restrictions");
				break;
			case duckdb_pgrouting::ArgKind::MATRIX_SQL:
				request.matrix_sql = StringValue::Get(input.inputs[i]);
				matrix_expr = Named(ListOfRows(context, request.matrix_sql), "matrix");
				break;
			case duckdb_pgrouting::ArgKind::COORDINATES_SQL:
				request.coordinates_sql = StringValue::Get(input.inputs[i]);
				coordinates_expr = Named(ListOfRows(context, request.coordinates_sql), "coordinates");
				break;
			case duckdb_pgrouting::ArgKind::ROOT_VID:
				SetRequestParameter(request, "root", input.inputs[i]);
				break;
			}
		}
		if (has_points) {
			// No plain 'edges' list: the two derived inputs partition the edge set, so the
			// materialized total stays about one edge set, scanned twice as under PostgreSQL.
			points_expr = Named(ListOfRows(context, request.points_sql), "points");
			edges_of_points_expr = Named(
			    ListOfRows(EdgesOfPoints(context, request.edges_sql, request.points_sql)), "edges_of_points");
			edges_no_points_expr = Named(
			    ListOfRows(EdgesWithoutPoints(context, request.edges_sql, request.points_sql)), "edges_no_points");
		} else if (has_edges) {
			edges_expr = Named(ListOfRows(context, request.edges_sql), "edges");
		}
	}

	row->select_list.push_back(std::move(edges_expr));
	row->select_list.push_back(std::move(combinations_expr));
	row->select_list.push_back(std::move(starts_expr));
	row->select_list.push_back(std::move(ends_expr));
	row->select_list.push_back(std::move(roots_expr));
	row->select_list.push_back(std::move(via_expr));
	for (auto &expr : id_list_exprs) {
		row->select_list.push_back(std::move(expr));
	}
	if (restrictions_expr) {
		row->select_list.push_back(std::move(restrictions_expr));
	}
	if (matrix_expr) {
		row->select_list.push_back(std::move(matrix_expr));
	}
	if (coordinates_expr) {
		row->select_list.push_back(std::move(coordinates_expr));
	}
	if (points_expr) {
		row->select_list.push_back(std::move(points_expr));
		row->select_list.push_back(std::move(edges_of_points_expr));
		row->select_list.push_back(std::move(edges_no_points_expr));
	}

	vector<unique_ptr<ParsedExpression>> args;
	args.push_back(ScalarSubquery(std::move(row))); // the TABLE argument
	for (auto &argument : RequestArguments(request)) {
		args.push_back(std::move(argument));
	}
	args.push_back(Named(Constant(Value::BOOLEAN(null_input)), "null_input"));

	auto fref = make_uniq<TableFunctionRef>();
	fref->function = make_uniq<FunctionExpression>(Identifier("_pgr_exec"), std::move(args));

	auto outer = make_uniq<SelectNode>();
	outer->select_list = ProjectionList(spec);
	outer->from_table = std::move(fref);
	if (spec.flags.projection == duckdb_pgrouting::Projection::COST_OF_PATH) {
		// The driver ran in path mode (see the COST_OF_PATH comment in function_spec.hpp); keep
		// only each path's closing row, identified the same way pgRouting's own SQL does: edge =
		// -1.
		outer->where_clause = make_uniq<ComparisonExpression>(
		    ExpressionType::COMPARE_EQUAL, make_uniq<ColumnRefExpression>(Identifier("edge")),
		    Constant(Value::BIGINT(-1)));
	}
	if (spec.flags.projection == duckdb_pgrouting::Projection::COST_SORTED) {
		auto order = make_uniq<OrderModifier>();
		for (const char *column : {"start_vid", "end_vid"}) {
			order->orders.emplace_back(OrderType::ASCENDING, OrderByNullType::NULLS_LAST,
			                           make_uniq<ColumnRefExpression>(Identifier(column)));
		}
		outer->modifiers.push_back(std::move(order));
	}
	return make_uniq<SubqueryRef>(WrapNode(std::move(outer)));
}

//===--------------------------------------------------------------------===//
// Catalog tags: which functions correspond to an upstream pgRouting function.
//===--------------------------------------------------------------------===//
void TagFunctions(ExtensionLoader &loader) {
	auto &db = loader.GetDatabaseInstance();
	auto &catalog = Catalog::GetSystemCatalog(db);
	auto transaction = CatalogTransaction::GetSystemTransaction(db);
	auto &schema = catalog.GetSchema(transaction, Identifier::DefaultSchema());
	for (const auto *table : SpecTables()) {
		for (const auto &spec : *table) {
			auto entry =
			    schema.GetEntry(transaction, CatalogType::TABLE_FUNCTION_ENTRY, Identifier(spec.upstream_name));
			if (!entry) {
				throw InternalException("pgrouting: function %s was not registered", spec.upstream_name);
			}
			auto &function_entry = entry->Cast<FunctionEntry>();
			// The tooling selects the upstream-equivalent functions by this tag, read back from
			// duckdb_functions(), instead of keeping its own list of them in a script.
			function_entry.tags.insert("ext", "pgrouting");
			function_entry.tags.insert("pgrouting_name", spec.upstream_name);
			if (spec.scalar_column != nullptr) {
				// The scalar macro is the same upstream function; untagged, collisions.test would
				// take it for another extension's name.
				auto macro = schema.GetEntry(transaction, CatalogType::MACRO_ENTRY, spec.upstream_name);
				if (!macro) {
					throw InternalException("pgrouting: scalar macro %s was not registered", spec.upstream_name);
				}
				auto &macro_entry = macro->Cast<FunctionEntry>();
				macro_entry.tags.insert("ext", "pgrouting");
				macro_entry.tags.insert("pgrouting_name", spec.upstream_name);
			}
		}
	}
}

// (SELECT <scalar_column> FROM <name>(<arg>, ..., <optional> := <optional>, ...)), a scalar macro of
// the table function's own name, one overload per argument count. The body is built from the spec
// row's fixed text, never a caller's, and parsed here once, at load; the table function it calls
// picks the overload by type, which the macro cannot. A defaulted parameter keeps its name and its
// default, so callers pass it positionally or by name as they would to the table function; a NULL
// argument yields no row there, and the scalar subquery then yields NULL, as a STRICT function
// returns. Two spec rows of one name and argument count must agree on the parameter names and the
// selected column, since they become one macro.
void RegisterScalarMacros(ExtensionLoader &loader, const string &name,
                          const vector<const duckdb_pgrouting::FunctionSpec *> &specs) {
	map<idx_t, const duckdb_pgrouting::FunctionSpec *> by_count;
	for (const auto *spec : specs) {
		const auto names = MacroParameterNames(*spec);
		auto entry = by_count.find(names.size());
		if (entry == by_count.end()) {
			by_count.emplace(names.size(), spec);
			continue;
		}
		if (MacroParameterNames(*entry->second) != names ||
		    string(entry->second->scalar_column) != spec->scalar_column) {
			throw InternalException("pgrouting: %s's scalar overloads of %d arguments do not agree on their names",
			                        name, names.size());
		}
	}

	CreateMacroInfo info(CatalogType::MACRO_ENTRY);
	info.SetSchema(Identifier::DefaultSchema());
	info.SetName(Identifier(name));
	info.internal = true;
	info.descriptions.push_back(duckdb_pgrouting::DescriptionOf(name));
	for (const auto &entry : by_count) {
		const auto &spec = *entry.second;
		auto macro = make_uniq<ScalarMacroFunction>();
		string arguments;
		auto add_argument = [&](const string &text) {
			arguments += arguments.empty() ? text : ", " + text;
		};
		for (auto kind : spec.args) {
			const string parameter = MacroParameterName(kind);
			macro->parameters.push_back(make_uniq<ColumnRefExpression>(Identifier(parameter)));
			add_argument(parameter);
		}
		for (const auto &param : spec.optionals) {
			const string parameter = param.name;
			macro->parameters.push_back(make_uniq<ColumnRefExpression>(Identifier(parameter)));
			macro->default_parameters.insert(make_pair(
			    Identifier(parameter), Constant(Value(param.default_value).DefaultCastAs(TypeOf(param.type)))));
			add_argument(parameter + " := " + parameter);
		}
		auto body = Parser::ParseExpressionList("(SELECT " + string(spec.scalar_column) + " FROM " + name + "(" +
		                                        arguments + "))");
		macro->expression = std::move(body[0]);
		info.macros.push_back(std::move(macro));
	}
	loader.RegisterFunction(info);
}

} // namespace

void RegisterSpecFunctions(ExtensionLoader &loader) {
	unordered_map<string, TableFunctionSet> sets;
	for (const auto *table : SpecTables()) {
		for (const auto &spec : *table) {
			CheckSpec(spec);
			const string name = spec.upstream_name;
			auto entry = sets.find(name);
			if (entry == sets.end()) {
				entry = sets.emplace(name, TableFunctionSet(Identifier(name))).first;
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
				fn.bind_replace = SpecBindReplace;
				// Every defaulted parameter is also accepted by name. A typed "**kwargs" keeps that out of
				// overload resolution, as named parameters were before DuckDB v2.0; keyword-only parameters
				// would let a named argument's cast cost pick a different variant.
				if (!spec.optionals.empty()) {
					fn.GetSignature().WithTypedKwargs("options", [&](TypedKwargs &options) {
						for (const auto &param : spec.optionals) {
							options.Add(param.name, TypeOf(param.type));
						}
					});
				}
				// The spec row travels in the function's info so bind_replace knows which overload
				// it is serving without re-deriving it from the argument types.
				fn.function_info = make_shared_ptr<SpecFunctionInfo>(spec);
				entry->second.AddFunction(fn);
			}
		}
	}
	for (auto &pair : sets) {
		CreateTableFunctionInfo info(std::move(pair.second));
		// One description with no parameter_types: duckdb_functions() then reports it for every
		// overload of the name.
		info.descriptions.push_back(duckdb_pgrouting::DescriptionOf(pair.first));
		// What ExtensionLoader::RegisterFunction(TableFunctionSet) sets before delegating here.
		info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
		loader.RegisterFunction(std::move(info));
	}
	// Scalar macros, grouped by name in spec-table order.
	vector<string> scalar_names;
	unordered_map<string, vector<const duckdb_pgrouting::FunctionSpec *>> scalar_specs;
	for (const auto *table : SpecTables()) {
		for (const auto &spec : *table) {
			if (spec.scalar_column == nullptr) {
				continue;
			}
			auto &group = scalar_specs[spec.upstream_name];
			if (group.empty()) {
				scalar_names.push_back(spec.upstream_name);
			}
			group.push_back(&spec);
		}
	}
	for (const auto &name : scalar_names) {
		RegisterScalarMacros(loader, name, scalar_specs[name]);
	}
	TagFunctions(loader);
}

} // namespace duckdb
