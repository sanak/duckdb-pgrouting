// SPDX-License-Identifier: GPL-2.0-or-later

// pgr_extractVertices(edges_sql, dryrun := false) -> (id, in_edges, out_edges, x, y, geom)
//
// Upstream writes this function in PL/pgSQL (sql/utilities/extractVertices.sql). It probes which
// of id, source, target, geom, startpoint and endpoint the edge query returns, chooses one of six
// queries, and runs it (or, with dryrun, only prints it). This file does the same at bind time.
// The six queries below are DuckDB translations of upstream's and must be re-diffed against it
// at every pgRouting bump. Differences: edge lists are always sorted ascending (upstream sorts
// them only in the geometry modes), the result is ordered by id, and a geometry is grouped by
// its coordinates rather than by geometry equality, which is the same thing for points.

#include "pgrouting/register.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include "sql_template.hpp"

namespace duckdb {

namespace {

using duckdb_pgrouting::CheckColumnType;
using duckdb_pgrouting::ThrowMissingColumn;

constexpr const char *FUNCTION_NAME = "pgr_extractVertices";

// Every template reads the caller's query through the CTE main_sql, whose placeholder body
// ReplaceCTE swaps for the parsed caller SQL. MATERIALIZED runs the caller's query once even
// where the template reads it twice.

constexpr const char *FROM_GEOM_WITH_ID = R"(
WITH
main_sql AS MATERIALIZED (SELECT 1),
the_out AS (SELECT CAST(id AS BIGINT) AS out_edge, ST_StartPoint(geom) AS point FROM main_sql),
agg_out AS (
  SELECT ST_X(point) AS x, ST_Y(point) AS y, list(out_edge ORDER BY out_edge) AS out_edges,
         any_value(point) AS point
  FROM the_out GROUP BY ALL),
the_in AS (SELECT CAST(id AS BIGINT) AS in_edge, ST_EndPoint(geom) AS point FROM main_sql),
agg_in AS (
  SELECT ST_X(point) AS x, ST_Y(point) AS y, list(in_edge ORDER BY in_edge) AS in_edges,
         any_value(point) AS point
  FROM the_in GROUP BY ALL),
the_points AS (
  SELECT x, y, in_edges, out_edges, coalesce(agg_out.point, agg_in.point) AS point
  FROM agg_out FULL OUTER JOIN agg_in USING (x, y))
SELECT CAST(row_number() OVER (ORDER BY x, y) AS BIGINT) AS id, in_edges, out_edges, x, y,
       point AS geom
FROM the_points
ORDER BY id
)";

constexpr const char *FROM_GEOM = R"(
WITH
main_sql AS MATERIALIZED (SELECT 1),
the_ends AS (
  SELECT ST_StartPoint(geom) AS point FROM main_sql
  UNION ALL
  SELECT ST_EndPoint(geom) FROM main_sql),
the_points AS (
  SELECT ST_X(point) AS x, ST_Y(point) AS y, any_value(point) AS point FROM the_ends GROUP BY ALL)
SELECT CAST(row_number() OVER (ORDER BY x, y) AS BIGINT) AS id,
       CAST(NULL AS BIGINT[]) AS in_edges, CAST(NULL AS BIGINT[]) AS out_edges, x, y,
       point AS geom
FROM the_points
ORDER BY id
)";

constexpr const char *FROM_POINTS_WITH_ID = R"(
WITH
main_sql AS MATERIALIZED (SELECT 1),
the_out AS (SELECT CAST(id AS BIGINT) AS out_edge, startpoint AS point FROM main_sql),
agg_out AS (
  SELECT ST_X(point) AS x, ST_Y(point) AS y, list(out_edge ORDER BY out_edge) AS out_edges,
         any_value(point) AS point
  FROM the_out GROUP BY ALL),
the_in AS (SELECT CAST(id AS BIGINT) AS in_edge, endpoint AS point FROM main_sql),
agg_in AS (
  SELECT ST_X(point) AS x, ST_Y(point) AS y, list(in_edge ORDER BY in_edge) AS in_edges,
         any_value(point) AS point
  FROM the_in GROUP BY ALL),
the_points AS (
  SELECT x, y, in_edges, out_edges, coalesce(agg_out.point, agg_in.point) AS point
  FROM agg_out FULL OUTER JOIN agg_in USING (x, y))
SELECT CAST(row_number() OVER (ORDER BY x, y) AS BIGINT) AS id, in_edges, out_edges, x, y,
       point AS geom
FROM the_points
ORDER BY id
)";

constexpr const char *FROM_POINTS = R"(
WITH
main_sql AS MATERIALIZED (SELECT 1),
the_ends AS (
  SELECT startpoint AS point FROM main_sql
  UNION ALL
  SELECT endpoint FROM main_sql),
the_points AS (
  SELECT ST_X(point) AS x, ST_Y(point) AS y, any_value(point) AS point FROM the_ends GROUP BY ALL)
SELECT CAST(row_number() OVER (ORDER BY x, y) AS BIGINT) AS id,
       CAST(NULL AS BIGINT[]) AS in_edges, CAST(NULL AS BIGINT[]) AS out_edges, x, y,
       point AS geom
FROM the_points
ORDER BY id
)";

constexpr const char *FROM_SOURCE_TARGET_WITH_ID = R"(
WITH
main_sql AS MATERIALIZED (SELECT 1),
agg_out AS (
  SELECT source AS vid, list(CAST(id AS BIGINT) ORDER BY id) AS out_edges
  FROM main_sql GROUP BY source),
agg_in AS (
  SELECT target AS vid, list(CAST(id AS BIGINT) ORDER BY id) AS in_edges
  FROM main_sql GROUP BY target),
the_points AS (
  SELECT vid, in_edges, out_edges FROM agg_out FULL OUTER JOIN agg_in USING (vid))
SELECT CAST(vid AS BIGINT) AS id, in_edges, out_edges, CAST(NULL AS DOUBLE) AS x,
       CAST(NULL AS DOUBLE) AS y, CAST(NULL AS GEOMETRY) AS geom
FROM the_points
ORDER BY id
)";

constexpr const char *FROM_SOURCE_TARGET = R"(
WITH
main_sql AS MATERIALIZED (SELECT 1),
the_points AS (SELECT source AS vid FROM main_sql UNION SELECT target FROM main_sql)
SELECT CAST(vid AS BIGINT) AS id, CAST(NULL AS BIGINT[]) AS in_edges,
       CAST(NULL AS BIGINT[]) AS out_edges, CAST(NULL AS DOUBLE) AS x, CAST(NULL AS DOUBLE) AS y,
       CAST(NULL AS GEOMETRY) AS geom
FROM the_points
ORDER BY id
)";

// The result's shape with no rows: a STRICT call with a NULL argument, and every dryrun.
constexpr const char *EMPTY_RESULT =
    "SELECT CAST(NULL AS BIGINT) AS id, CAST(NULL AS BIGINT[]) AS in_edges, "
    "CAST(NULL AS BIGINT[]) AS out_edges, CAST(NULL AS DOUBLE) AS x, CAST(NULL AS DOUBLE) AS y, "
    "CAST(NULL AS GEOMETRY) AS geom WHERE false";

unique_ptr<TableRef> ExtractVerticesBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	// STRICT upstream: a NULL argument, positional or named, gives no rows.
	bool null_input = input.inputs[0].IsNull();
	bool dryrun = false;
	if (input.inputs.size() > 1) {
		null_input = null_input || input.inputs[1].IsNull();
		dryrun = !input.inputs[1].IsNull() && BooleanValue::Get(input.inputs[1]);
	}
	auto named = input.named_parameters.find("dryrun");
	if (named != input.named_parameters.end()) {
		null_input = null_input || named->second.IsNull();
		dryrun = !named->second.IsNull() && BooleanValue::Get(named->second);
	}
	if (null_input) {
		return duckdb_pgrouting::AsTableRef(duckdb_pgrouting::ParseSingleSelect(context, EMPTY_RESULT));
	}

	const auto sql = StringValue::Get(input.inputs[0]);
	const auto columns = duckdb_pgrouting::BindColumns(context, sql);
	const auto *id = duckdb_pgrouting::FindColumn(columns, "id");
	const auto *source = duckdb_pgrouting::FindColumn(columns, "source");
	const auto *target = duckdb_pgrouting::FindColumn(columns, "target");
	const auto *geom = duckdb_pgrouting::FindColumn(columns, "geom");
	const auto *startpoint = duckdb_pgrouting::FindColumn(columns, "startpoint");
	const auto *endpoint = duckdb_pgrouting::FindColumn(columns, "endpoint");

	// Upstream's dryrun asks _pgr_checkColumn only whether a column exists, never its type.
	if (!dryrun) {
		CheckColumnType(id, "id", true, sql);
		CheckColumnType(source, "source", true, sql);
		CheckColumnType(target, "target", true, sql);
		CheckColumnType(geom, "geom", false, sql);
		CheckColumnType(startpoint, "startpoint", false, sql);
		CheckColumnType(endpoint, "endpoint", false, sql);
	}
	if (!geom) {
		if (target && !source) {
			ThrowMissingColumn("source", sql);
		}
		if (source && !target) {
			ThrowMissingColumn("target", sql);
		}
		if (startpoint && !endpoint) {
			ThrowMissingColumn("endpoint", sql);
		}
		if (endpoint && !startpoint) {
			ThrowMissingColumn("startpoint", sql);
		}
		if (!source && !startpoint) {
			ThrowMissingColumn("geom", sql);
		}
	}

	// Upstream's precedence: geom, then startpoint/endpoint, then source/target.
	const char *query_text;
	bool needs_spatial = true;
	if (geom) {
		query_text = id ? FROM_GEOM_WITH_ID : FROM_GEOM;
	} else if (startpoint) {
		query_text = id ? FROM_POINTS_WITH_ID : FROM_POINTS;
	} else {
		query_text = id ? FROM_SOURCE_TARGET_WITH_ID : FROM_SOURCE_TARGET;
		needs_spatial = false;
	}
	auto statement = duckdb_pgrouting::ParseSingleSelect(context, query_text);
	duckdb_pgrouting::ReplaceCTE(*statement, "main_sql", duckdb_pgrouting::ParseSingleSelect(context, sql));
	if (dryrun) {
		duckdb_pgrouting::LogDryrun(context, *statement);
		return duckdb_pgrouting::AsTableRef(duckdb_pgrouting::ParseSingleSelect(context, EMPTY_RESULT));
	}
	if (needs_spatial) {
		duckdb_pgrouting::RequireSpatial(context, FUNCTION_NAME);
	}
	return duckdb_pgrouting::AsTableRef(std::move(statement));
}

} // namespace

void RegisterExtractVertices(ExtensionLoader &loader) {
	TableFunctionSet set(FUNCTION_NAME);
	// Upstream's one defaulted parameter, dryrun, may be passed positionally or by name, so the
	// signature is registered twice (see RegisterShortestPathFunctions).
	for (idx_t positional = 0; positional <= 1; positional++) {
		vector<LogicalType> arguments {LogicalType::VARCHAR};
		if (positional == 1) {
			arguments.push_back(LogicalType::BOOLEAN);
		}
		TableFunction function(arguments, nullptr, nullptr);
		function.bind_replace = ExtractVerticesBindReplace;
		function.named_parameters["dryrun"] = LogicalType::BOOLEAN;
		set.AddFunction(function);
	}
	// Tagged spatial although the source/target modes do not need it: the tag tells the tooling to
	// load spatial before running any of this function's documented queries.
	duckdb_pgrouting::RegisterTemplateFunction(loader, std::move(set), true);
}

} // namespace duckdb
