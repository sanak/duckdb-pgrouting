// SPDX-License-Identifier: GPL-2.0-or-later

#include "sql_template.hpp"

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/function_entry.hpp"
#include "duckdb/catalog/catalog_entry/schema_catalog_entry.hpp"
#include "duckdb/catalog/catalog_transaction.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/identifier.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/logging/logger.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/tableref/subqueryref.hpp"
#include "duckdb/planner/binder.hpp"

#include "function_docs.hpp"

namespace duckdb_pgrouting {

using namespace duckdb;

unique_ptr<SelectStatement> ParseSingleSelect(ClientContext &context, const string &sql) {
	Parser parser(context.GetParserOptions());
	parser.ParseQuery(sql);
	if (parser.statements.size() != 1 || parser.statements[0]->type != StatementType::SELECT_STATEMENT) {
		throw InvalidInputException("Expected a single SELECT statement: %s", sql);
	}
	return unique_ptr_cast<SQLStatement, SelectStatement>(std::move(parser.statements[0]));
}

void ReplaceCTE(SelectStatement &statement, const char *name, unique_ptr<SelectStatement> query) {
	auto &ctes = statement.node->cte_map.map;
	auto entry = ctes.find(Identifier(name));
	if (entry == ctes.end()) {
		throw InternalException("pgrouting: SQL template has no CTE named %s", name);
	}
	entry->second->query_node = std::move(query->node);
}

vector<QueryColumn> BindColumns(ClientContext &context, const string &sql) {
	auto statement = ParseSingleSelect(context, sql);
	auto binder = Binder::CreateBinder(context);
	// Binder::Bind(SelectStatement &) is private; the public SQLStatement overload dispatches to it.
	auto bound = binder->Bind(static_cast<SQLStatement &>(*statement));
	vector<QueryColumn> columns;
	for (idx_t i = 0; i < bound.names.size(); i++) {
		columns.push_back(QueryColumn {bound.names[i].GetIdentifierName(), bound.types[i]});
	}
	return columns;
}

const QueryColumn *FindColumn(const vector<QueryColumn> &columns, const char *name) {
	for (auto &column : columns) {
		if (StringUtil::CIEquals(column.name, name)) {
			return &column;
		}
	}
	return nullptr;
}

bool IsAnyInteger(const LogicalType &type) {
	switch (type.id()) {
	case LogicalTypeId::TINYINT:
	case LogicalTypeId::SMALLINT:
	case LogicalTypeId::INTEGER:
	case LogicalTypeId::BIGINT:
	case LogicalTypeId::UTINYINT:
	case LogicalTypeId::USMALLINT:
	case LogicalTypeId::UINTEGER:
		return true;
	default:
		return false;
	}
}

bool IsGeometry(const LogicalType &type) {
	return type.id() == LogicalTypeId::GEOMETRY;
}

void ThrowMissingColumn(const char *name, const string &sql) {
	throw InvalidInputException("column \"%s\" does not exist\nHINT: %s", name, sql);
}

void CheckColumnType(const QueryColumn *column, const char *name, bool integer, const string &sql) {
	if (!column || (integer ? IsAnyInteger(column->type) : IsGeometry(column->type))) {
		return;
	}
	throw InvalidInputException("Expected type of column \"%s\" is %s\nHINT: Query: %s", name,
	                            integer ? "ANY-INTEGER" : "geometry", sql);
}

unique_ptr<TableRef> AsTableRef(unique_ptr<SelectStatement> statement) {
	return make_uniq<SubqueryRef>(std::move(statement));
}

namespace {

// ST_StartPoint stands for the whole extension: every geometry template calls it.
bool SpatialIsLoaded(ClientContext &context) {
	return Catalog::GetSystemCatalog(context).GetEntry(context, CatalogType::SCALAR_FUNCTION_ENTRY,
	                                                   Identifier::DefaultSchema(), Identifier("st_startpoint"),
	                                                   OnEntryNotFound::RETURN_NULL) != nullptr;
}

bool AutoloadEnabled(ClientContext &context) {
	Value value;
	return context.TryGetCurrentSetting(Identifier("autoload_known_extensions"), value) && !value.IsNull() &&
	       BooleanValue::Get(value);
}

} // namespace

void RequireSpatial(ClientContext &context, const char *function_name) {
	if (SpatialIsLoaded(context)) {
		return;
	}
	// TryAutoLoadExtension ignores autoload_known_extensions and would load spatial (installing it
	// first when autoinstall_known_extensions is on) even for a caller who turned autoloading off, so
	// the setting is checked here first.
	if (AutoloadEnabled(context) && ExtensionHelper::TryAutoLoadExtension(context, "spatial") &&
	    SpatialIsLoaded(context)) {
		return;
	}
	throw InvalidInputException("%s needs the spatial extension: INSTALL spatial; LOAD spatial", function_name);
}

void LogDryrun(ClientContext &context, const SelectStatement &statement) {
	// The two-argument macro form passes the message through unchanged (see exec_common.cpp).
	const string message = statement.ToString() + ";";
	DUCKDB_LOG_INFO(context, message);
}

void RegisterTemplateFunction(ExtensionLoader &loader, TableFunctionSet set, bool needs_spatial) {
	const string name = set.name.GetIdentifierName();
	CreateTableFunctionInfo info(std::move(set));
	info.descriptions.push_back(DescriptionOf(name));
	// What ExtensionLoader::RegisterFunction(TableFunctionSet) sets before delegating here.
	info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
	loader.RegisterFunction(std::move(info));

	auto &db = loader.GetDatabaseInstance();
	auto &catalog = Catalog::GetSystemCatalog(db);
	auto transaction = CatalogTransaction::GetSystemTransaction(db);
	auto &schema = catalog.GetSchema(transaction, Identifier::DefaultSchema());
	auto entry = schema.GetEntry(transaction, CatalogType::TABLE_FUNCTION_ENTRY, Identifier(name));
	if (!entry) {
		throw InternalException("pgrouting: function %s was not registered", name);
	}
	auto &function_entry = entry->Cast<FunctionEntry>();
	function_entry.tags.insert("ext", "pgrouting");
	function_entry.tags.insert("pgrouting_name", name);
	if (needs_spatial) {
		// Read by scripts/gen_docqueries_tests.py and the catalog-example test, which load spatial
		// before running such a function's queries.
		function_entry.tags.insert("pgrouting_requires", "spatial");
	}
}

} // namespace duckdb_pgrouting
