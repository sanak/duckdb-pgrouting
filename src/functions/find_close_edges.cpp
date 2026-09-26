// SPDX-License-Identifier: GPL-2.0-or-later

// pgr_findCloseEdges(edges_sql, point | points, tolerance, cap := 1, dryrun := false)
//   -> (edge_id, fraction, side, distance, geom, edge)
//
// Upstream writes this function in PL/pgSQL (sql/utilities/findCloseEdges.sql). For each point it
// returns up to `cap` edges within `tolerance`, nearest first: where along the edge the closest
// point lies (fraction), which side of the edge the point is on, the distance, the point itself
// and the connecting segment. This file builds the same query at bind time. The query below is
// a DuckDB translation of upstream's and must be re-diffed against it at every pgRouting bump.
// Differences:
// - side: spatial's ST_Buffer has no single-sided option, so instead of intersecting the point
//   with upstream's right-side flat-ended buffer, side is the sign of the cross product between
//   the edge's direction around the closest point and the vector to the point. Upstream's
//   corner cases are kept: a point on the line is 'r', a point past either end (fraction 0 or 1)
//   is 'l', and a tolerance of 0 (an empty buffer) gives 'l'. Near a vertex where the edge
//   turns, the two rules can disagree.
// - Rows are ordered by the point's position in the input, then by distance and edge id;
//   upstream leaves the order unspecified.

#include "pgrouting/register.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/identifier.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/query_node/select_node.hpp"
#include "duckdb/parser/tableref/emptytableref.hpp"

#include "sql_template.hpp"

namespace duckdb {

namespace {

using duckdb_pgrouting::CheckColumnType;
using duckdb_pgrouting::ThrowMissingColumn;

constexpr const char *FUNCTION_NAME = "pgr_findCloseEdges";

// edges_sql's placeholder body becomes the caller's parsed query and arguments' becomes one row
// of constants (points, tolerance, cap). Every column read from the caller's query is qualified,
// so a caller column called point, tolerance or cap cannot make a reference ambiguous.
constexpr const char *FIND_CLOSE_EDGES = R"(
WITH
edges_sql AS (SELECT 1),
arguments AS (SELECT 1),
point_sql AS (
  SELECT unnest(points) AS point, unnest(range(1, len(points) + 1)) AS point_index, tolerance, cap
  FROM arguments),
results AS (
  SELECT CAST(edges_sql.id AS BIGINT) AS edge_id, edges_sql.geom AS line,
         point_sql.point AS point, point_sql.point_index AS point_index,
         point_sql.tolerance AS tolerance, point_sql.cap AS cap,
         ST_LineLocatePoint(edges_sql.geom, point_sql.point) AS fraction,
         ST_Distance(edges_sql.geom, point_sql.point) AS distance
  FROM edges_sql, point_sql
  WHERE ST_DWithin(edges_sql.geom, point_sql.point, point_sql.tolerance)),
ranked AS (
  SELECT *,
         ST_LineInterpolatePoint(line, greatest(fraction - 1e-6, 0.0)) AS behind,
         ST_LineInterpolatePoint(line, least(fraction + 1e-6, 1.0)) AS ahead,
         row_number() OVER (PARTITION BY point_index ORDER BY distance, edge_id) AS rn
  FROM results)
SELECT edge_id, fraction,
       CASE WHEN tolerance > 0 AND fraction > 0 AND fraction < 1
                 AND (ST_X(ahead) - ST_X(behind)) * (ST_Y(point) - ST_Y(behind))
                     - (ST_Y(ahead) - ST_Y(behind)) * (ST_X(point) - ST_X(behind)) <= 0
            THEN 'r' ELSE 'l' END AS side,
       distance, point AS geom, ST_MakeLine(point, ST_ClosestPoint(line, point)) AS edge
FROM ranked
WHERE rn <= cap
ORDER BY point_index, rn
)";

constexpr const char *EMPTY_RESULT =
    "SELECT CAST(NULL AS BIGINT) AS edge_id, CAST(NULL AS DOUBLE) AS fraction, "
    "CAST(NULL AS VARCHAR) AS side, CAST(NULL AS DOUBLE) AS distance, CAST(NULL AS GEOMETRY) AS geom, "
    "CAST(NULL AS GEOMETRY) AS edge WHERE false";

unique_ptr<ParsedExpression> Aliased(Value value, const char *alias) {
	auto expression = ConstantExpression::FromValue(value);
	expression->SetAlias(Identifier(alias));
	return expression;
}

// SELECT <points> AS points, <tolerance> AS tolerance, <cap> AS cap
unique_ptr<SelectStatement> Arguments(Value points, double tolerance, int32_t cap) {
	auto node = make_uniq<SelectNode>();
	node->select_list.push_back(Aliased(std::move(points), "points"));
	node->select_list.push_back(Aliased(Value::DOUBLE(tolerance), "tolerance"));
	node->select_list.push_back(Aliased(Value::INTEGER(cap), "cap"));
	node->from_table = make_uniq<EmptyTableRef>();
	auto statement = make_uniq<SelectStatement>();
	statement->node = std::move(node);
	return statement;
}

unique_ptr<TableRef> Empty(ClientContext &context) {
	return duckdb_pgrouting::AsTableRef(duckdb_pgrouting::ParseSingleSelect(context, EMPTY_RESULT));
}

unique_ptr<TableRef> FindCloseEdgesBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	// STRICT upstream: a NULL argument, positional or named, gives no rows.
	bool null_input = false;
	for (auto &value : input.inputs) {
		null_input = null_input || value.IsNull();
	}
	int32_t cap = 1;
	bool dryrun = false;
	if (input.inputs.size() > 3 && !input.inputs[3].IsNull()) {
		cap = IntegerValue::Get(input.inputs[3]);
	}
	if (input.inputs.size() > 4 && !input.inputs[4].IsNull()) {
		dryrun = BooleanValue::Get(input.inputs[4]);
	}
	auto named_cap = input.named_parameters.find("cap");
	if (named_cap != input.named_parameters.end()) {
		null_input = null_input || named_cap->second.IsNull();
		cap = named_cap->second.IsNull() ? cap : IntegerValue::Get(named_cap->second);
	}
	auto named_dryrun = input.named_parameters.find("dryrun");
	if (named_dryrun != input.named_parameters.end()) {
		null_input = null_input || named_dryrun->second.IsNull();
		dryrun = !named_dryrun->second.IsNull() && BooleanValue::Get(named_dryrun->second);
	}
	if (null_input) {
		return Empty(context);
	}

	const double tolerance = DoubleValue::Get(input.inputs[2]);
	if (tolerance < 0) {
		throw InvalidInputException("Invalid value for \"tolerance\"");
	}
	if (cap < 0) {
		throw InvalidInputException("Invalid value for \"cap\"");
	}

	// Upstream checks id and geom as required columns, so a missing one is an error even with
	// dryrun; their types are checked only without it.
	const auto sql = StringValue::Get(input.inputs[0]);
	const auto columns = duckdb_pgrouting::BindColumns(context, sql);
	const auto *id = duckdb_pgrouting::FindColumn(columns, "id");
	if (!id) {
		ThrowMissingColumn("id", sql);
	}
	if (!dryrun) {
		CheckColumnType(id, "id", true, sql);
	}
	const auto *geom = duckdb_pgrouting::FindColumn(columns, "geom");
	if (!geom) {
		ThrowMissingColumn("geom", sql);
	}
	if (!dryrun) {
		CheckColumnType(geom, "geom", false, sql);
	}

	// The one-point signature is upstream's array signature called with ARRAY[point].
	const auto &point_argument = input.inputs[1];
	Value points = point_argument.type().id() == LogicalTypeId::LIST
	                   ? point_argument
	                   : Value::LIST(point_argument.type(), {point_argument});

	auto statement = duckdb_pgrouting::ParseSingleSelect(context, FIND_CLOSE_EDGES);
	duckdb_pgrouting::ReplaceCTE(*statement, "edges_sql", duckdb_pgrouting::ParseSingleSelect(context, sql));
	duckdb_pgrouting::ReplaceCTE(*statement, "arguments", Arguments(std::move(points), tolerance, cap));
	if (dryrun) {
		duckdb_pgrouting::LogDryrun(context, *statement);
		return Empty(context);
	}
	duckdb_pgrouting::RequireSpatial(context, FUNCTION_NAME);
	return duckdb_pgrouting::AsTableRef(std::move(statement));
}

} // namespace

void RegisterFindCloseEdges(ExtensionLoader &loader) {
	TableFunctionSet set(FUNCTION_NAME);
	// Upstream's two signatures differ in the point argument: one GEOMETRY or a GEOMETRY[]. Each
	// has two defaulted parameters (cap, dryrun), so each is registered three times, taking the
	// first 0, 1 or 2 of them positionally (see RegisterShortestPathFunctions).
	const vector<LogicalType> point_types {LogicalType::GEOMETRY(), LogicalType::LIST(LogicalType::GEOMETRY())};
	const vector<LogicalType> optional_types {LogicalType::INTEGER, LogicalType::BOOLEAN};
	for (const auto &point_type : point_types) {
		for (idx_t positional = 0; positional <= optional_types.size(); positional++) {
			vector<LogicalType> arguments {LogicalType::VARCHAR, point_type, LogicalType::DOUBLE};
			for (idx_t i = 0; i < positional; i++) {
				arguments.push_back(optional_types[i]);
			}
			TableFunction function(arguments, nullptr, nullptr);
			function.bind_replace = FindCloseEdgesBindReplace;
			function.named_parameters["cap"] = LogicalType::INTEGER;
			function.named_parameters["dryrun"] = LogicalType::BOOLEAN;
			set.AddFunction(function);
		}
	}
	duckdb_pgrouting::RegisterTemplateFunction(loader, std::move(set), true);
}

} // namespace duckdb
