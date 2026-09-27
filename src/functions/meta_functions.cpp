// SPDX-License-Identifier: GPL-2.0-or-later

#include "pgrouting/register.hpp"

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/function_entry.hpp"
#include "duckdb/catalog/catalog_entry/schema_catalog_entry.hpp"
#include "duckdb/catalog/catalog_transaction.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/identifier.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"

#include "function_docs.hpp"

namespace duckdb {

namespace {

// PROJECT_VERSION is the bundled pgRouting version, read from upstream's own project() call by
// cmake/pgrouting_sources.cmake. The extension's own version is a different thing and comes from
// duckdb_extensions().extension_version.
void PgRoutingVersionFunction(DataChunk &args, ExpressionState &, Vector &result) {
	D_ASSERT(args.ColumnCount() == 0);
	auto value = Value(PROJECT_VERSION);
	// Reference sets the vector type itself.
	result.Reference(value, count_t(args.size()));
}

} // namespace

void RegisterMetaFunctions(ExtensionLoader &loader) {
	CreateScalarFunctionInfo version(
	    ScalarFunction("pgr_version", {}, LogicalType::VARCHAR, PgRoutingVersionFunction));
	version.descriptions.push_back(duckdb_pgrouting::DescriptionOf("pgr_version"));
	version.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
	loader.RegisterFunction(std::move(version));

	// pgr_version is upstream's own function, so it carries pgrouting_name like every other
	// upstream-equivalent function: check_signatures.py and the docqueries generator select it by
	// that tag.
	auto &db = loader.GetDatabaseInstance();
	auto &catalog = Catalog::GetSystemCatalog(db);
	auto transaction = CatalogTransaction::GetSystemTransaction(db);
	auto &schema = catalog.GetSchema(transaction, Identifier::DefaultSchema());
	auto entry = schema.GetEntry(transaction, CatalogType::SCALAR_FUNCTION_ENTRY, Identifier("pgr_version"));
	if (!entry) {
		throw InternalException("pgrouting: pgr_version was not registered");
	}
	auto &function_entry = entry->Cast<FunctionEntry>();
	function_entry.tags.insert("ext", "pgrouting");
	function_entry.tags.insert("pgrouting_name", "pgr_version");
}

} // namespace duckdb
