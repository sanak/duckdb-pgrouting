// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Shared machinery of the public functions that upstream writes in PL/pgSQL (pgr_extractVertices,
// pgr_findCloseEdges). Each is a bind_replace table function: it binds the caller's edge query to
// learn its columns, picks one fixed SQL template, and splices the caller's SQL into it as a parsed
// CTE and its arguments as constants, never as text.

#include "duckdb.hpp"
#include "duckdb/function/function_set.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/tableref.hpp"

namespace duckdb {
class ExtensionLoader;
}

namespace duckdb_pgrouting {

// `sql` parsed on its own as exactly one SELECT; InvalidInputException otherwise.
duckdb::unique_ptr<duckdb::SelectStatement> ParseSingleSelect(duckdb::ClientContext &context,
                                                              const duckdb::string &sql);

// Swaps the query of the CTE `name` of a statement parsed from a fixed template for `query`,
// keeping the CTE's own options (MATERIALIZED). InternalException when the template has no such CTE.
void ReplaceCTE(duckdb::SelectStatement &statement, const char *name,
                duckdb::unique_ptr<duckdb::SelectStatement> query);

struct QueryColumn {
	duckdb::string name;
	duckdb::LogicalType type;
};

// The result columns of `sql`, found by binding it without running it; upstream's
// _pgr_checkColumn runs the query with LIMIT 1 for the same answer.
duckdb::vector<QueryColumn> BindColumns(duckdb::ClientContext &context, const duckdb::string &sql);

// The column called `name`, compared case-insensitively as DuckDB resolves names; nullptr if absent.
const QueryColumn *FindColumn(const duckdb::vector<QueryColumn> &columns, const char *name);

// Upstream's ANY-INTEGER, as the exec function reads it (shortest_path_exec.cpp ClassOf): the
// signed types and the unsigned ones that fit in int64_t.
bool IsAnyInteger(const duckdb::LogicalType &type);
bool IsGeometry(const duckdb::LogicalType &type);

// Upstream's _pgr_checkColumn errors, verbatim: 'column "<name>" does not exist' with the
// caller's query as the hint, and 'Expected type of column "<name>" is ANY-INTEGER' (or
// "geometry") with "Query: <sql>" as the hint. CheckColumnType accepts an absent column.
[[noreturn]] void ThrowMissingColumn(const char *name, const duckdb::string &sql);
void CheckColumnType(const QueryColumn *column, const char *name, bool integer, const duckdb::string &sql);

// A bind_replace result that is `statement`.
duckdb::unique_ptr<duckdb::TableRef> AsTableRef(duckdb::unique_ptr<duckdb::SelectStatement> statement);

// Returns when duckdb-spatial's functions are in the catalog. spatial is not autoloadable in DuckDB,
// so a missing ST_ function never loads it; when the caller's autoload_known_extensions is on,
// spatial is loaded here instead, and installed first when autoinstall_known_extensions is on too.
// Otherwise throws "<function_name> needs the spatial extension: INSTALL spatial; LOAD spatial".
void RequireSpatial(duckdb::ClientContext &context, const char *function_name);

// What upstream's dryrun raises as a NOTICE: the generated query, logged at INFO.
void LogDryrun(duckdb::ClientContext &context, const duckdb::SelectStatement &statement);

// Registers `set` under its (upstream) name with its catalog description and the tags the tooling
// reads: ext = pgrouting, pgrouting_name = the name, and pgrouting_requires = spatial when
// `needs_spatial`.
void RegisterTemplateFunction(duckdb::ExtensionLoader &loader, duckdb::TableFunctionSet set,
                              bool needs_spatial);

} // namespace duckdb_pgrouting
