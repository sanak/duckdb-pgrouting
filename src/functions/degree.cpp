// SPDX-License-Identifier: GPL-2.0-or-later

// pgr_degree(edges_sql, dryrun := false) -> (node, degree)
// pgr_degree(edges_sql, vertices_sql, dryrun := false) -> (node, degree)
//
// Upstream writes both in PL/pgSQL (sql/metrics/degree.sql). The first counts, for every vertex, the
// rows of the edge query whose source or target it is (a self-loop twice). The second counts, for
// every row of the vertex query, the entries of its in_edges and out_edges lists that are ids of the
// edge query's rows. Each checks its columns with _pgr_checkColumn, builds one query and runs it (or,
// with dryrun, only prints it); this file does the same at bind time. The queries below are DuckDB
// translations of upstream's and must be re-diffed against it at every pgRouting bump. Difference:
// the result is ordered by node (upstream's has no ORDER BY).

#include "pgrouting/register.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include "sql_template.hpp"

namespace duckdb {

namespace {

using duckdb_pgrouting::AsTableRef;
using duckdb_pgrouting::BindColumns;
using duckdb_pgrouting::CheckColumnType;
using duckdb_pgrouting::CheckIntegerListColumn;
using duckdb_pgrouting::FindColumn;
using duckdb_pgrouting::ParseSingleSelect;
using duckdb_pgrouting::ReplaceCTE;
using duckdb_pgrouting::ThrowMissingColumn;

constexpr const char *FUNCTION_NAME = "pgr_degree";

// The caller's queries replace the placeholder bodies of the CTEs g_edges and all_vertices
// (ReplaceCTE); MATERIALIZED runs each once.
constexpr const char *FROM_EDGES = R"(
WITH
g_edges AS MATERIALIZED (SELECT 1),
g_vertices AS (
  SELECT source AS node FROM g_edges
  UNION ALL
  SELECT target FROM g_edges)
SELECT CAST(node AS BIGINT) AS node, CAST(count(*) AS BIGINT) AS degree
FROM g_vertices
GROUP BY node
ORDER BY node
)";

// EDGE_IDS is replaced by one of the three fixed expressions below, as upstream's format() fills
// its %s: which of in_edges / out_edges the vertex query has decides it.
constexpr const char *FROM_VERTICES = R"(
WITH
g_edges AS MATERIALIZED (SELECT 1),
all_vertices AS MATERIALIZED (SELECT 1),
g_vertices AS (SELECT id, unnest(EDGE_IDS) AS eid FROM all_vertices),
totals AS (
  SELECT v.id, count(*) AS count
  FROM g_vertices v JOIN g_edges e ON (v.eid = e.id)
  GROUP BY v.id)
SELECT CAST(id AS BIGINT) AS node, CAST(count AS BIGINT) AS degree
FROM all_vertices JOIN totals USING (id)
ORDER BY node
)";

constexpr const char *IN_EDGES = "coalesce(CAST(in_edges AS BIGINT[]), CAST([] AS BIGINT[]))";
constexpr const char *OUT_EDGES = "coalesce(CAST(out_edges AS BIGINT[]), CAST([] AS BIGINT[]))";

// The result's shape with no rows: a STRICT call with a NULL argument, and every dryrun.
constexpr const char *EMPTY_RESULT =
    "SELECT CAST(NULL AS BIGINT) AS node, CAST(NULL AS BIGINT) AS degree WHERE false";

struct DegreeCall {
	bool null_input = false;
	bool dryrun = false;
};

// STRICT upstream: a NULL argument, positional or named, gives no rows. dryrun comes positionally
// at dryrun_index or by name.
DegreeCall ReadCall(const TableFunctionBindInput &input, idx_t dryrun_index) {
	DegreeCall call;
	for (auto &value : input.inputs) {
		call.null_input = call.null_input || value.IsNull();
	}
	if (input.inputs.size() > dryrun_index && !input.inputs[dryrun_index].IsNull()) {
		call.dryrun = BooleanValue::Get(input.inputs[dryrun_index]);
	}
	auto named = input.named_parameters.find("dryrun");
	if (named != input.named_parameters.end()) {
		call.null_input = call.null_input || named->second.IsNull();
		call.dryrun = !named->second.IsNull() && BooleanValue::Get(named->second);
	}
	return call;
}

unique_ptr<TableRef> Empty(ClientContext &context) {
	return AsTableRef(ParseSingleSelect(context, EMPTY_RESULT));
}

// _pgr_checkColumn(sql, name, 'ANY-INTEGER', dryrun => dryrun): a missing column always raises
// (upstream's probe query fails before dryrun is looked at); the type is checked only without dryrun.
void RequireIntegerColumn(const vector<duckdb_pgrouting::QueryColumn> &columns, const char *name, const string &sql,
                          bool dryrun) {
	const auto *column = FindColumn(columns, name);
	if (!column) {
		ThrowMissingColumn(name, sql);
	}
	if (!dryrun) {
		CheckColumnType(column, name, true, sql);
	}
}

unique_ptr<TableRef> Finish(ClientContext &context, unique_ptr<SelectStatement> statement, bool dryrun) {
	if (dryrun) {
		duckdb_pgrouting::LogDryrun(context, *statement);
		return Empty(context);
	}
	return AsTableRef(std::move(statement));
}

unique_ptr<TableRef> DegreeFromEdgesBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	const auto call = ReadCall(input, 1);
	if (call.null_input) {
		return Empty(context);
	}
	const auto sql = StringValue::Get(input.inputs[0]);
	const auto columns = BindColumns(context, sql);
	for (const char *name : {"id", "source", "target"}) {
		RequireIntegerColumn(columns, name, sql, call.dryrun);
	}
	auto statement = ParseSingleSelect(context, FROM_EDGES);
	ReplaceCTE(*statement, "g_edges", ParseSingleSelect(context, sql));
	return Finish(context, std::move(statement), call.dryrun);
}

unique_ptr<TableRef> DegreeFromVerticesBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	const auto call = ReadCall(input, 2);
	if (call.null_input) {
		return Empty(context);
	}
	const auto edges_sql = StringValue::Get(input.inputs[0]);
	const auto vertices_sql = StringValue::Get(input.inputs[1]);
	RequireIntegerColumn(BindColumns(context, edges_sql), "id", edges_sql, call.dryrun);
	const auto vertex_columns = BindColumns(context, vertices_sql);
	RequireIntegerColumn(vertex_columns, "id", vertices_sql, call.dryrun);
	const auto *in_edges = FindColumn(vertex_columns, "in_edges");
	const auto *out_edges = FindColumn(vertex_columns, "out_edges");
	if (!call.dryrun) {
		CheckIntegerListColumn(in_edges, "in_edges", vertices_sql);
		CheckIntegerListColumn(out_edges, "out_edges", vertices_sql);
	}
	if (!in_edges && !out_edges) {
		ThrowMissingColumn("in_edges", vertices_sql);
	}
	// Fixed text only: EDGE_IDS becomes one of the expressions above, never caller SQL.
	string edge_ids = in_edges && out_edges ? StringUtil::Format("list_concat(%s, %s)", IN_EDGES, OUT_EDGES)
	                  : in_edges            ? string(IN_EDGES)
	                                        : string(OUT_EDGES);
	auto statement = ParseSingleSelect(context, StringUtil::Replace(FROM_VERTICES, "EDGE_IDS", edge_ids));
	ReplaceCTE(*statement, "g_edges", ParseSingleSelect(context, edges_sql));
	ReplaceCTE(*statement, "all_vertices", ParseSingleSelect(context, vertices_sql));
	return Finish(context, std::move(statement), call.dryrun);
}

} // namespace

void RegisterDegree(ExtensionLoader &loader) {
	TableFunctionSet set(FUNCTION_NAME);
	// Two upstream signatures, each with one defaulted parameter (dryrun) that may be passed
	// positionally or by name: four variants (see RegisterSpecFunctions).
	for (idx_t queries = 1; queries <= 2; queries++) {
		for (idx_t positional = 0; positional <= 1; positional++) {
			vector<LogicalType> arguments(queries, LogicalType::VARCHAR);
			if (positional == 1) {
				arguments.push_back(LogicalType::BOOLEAN);
			}
			TableFunction function(arguments, nullptr, nullptr);
			function.bind_replace = queries == 1 ? DegreeFromEdgesBindReplace : DegreeFromVerticesBindReplace;
			function.GetSignature().WithTypedKwargs(
			    "options", [](TypedKwargs &options) { options.Add("dryrun", LogicalType::BOOLEAN); });
			set.AddFunction(function);
		}
	}
	// Not tagged spatial: neither form needs it (its documentation's q2 does, and is skip-listed).
	duckdb_pgrouting::RegisterTemplateFunction(loader, std::move(set), false);
}

} // namespace duckdb
