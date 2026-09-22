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
	ScalarFunction version("DuckDB_pgRouting_Version", {}, LogicalType::VARCHAR, PgRoutingVersionFunction);
	loader.RegisterFunction(version);

	// Tagged like every other function this extension publishes, so the name-collision check can
	// select this extension's names by tag rather than by guessing at a prefix.
	auto &db = loader.GetDatabaseInstance();
	auto &catalog = Catalog::GetSystemCatalog(db);
	auto transaction = CatalogTransaction::GetSystemTransaction(db);
	auto &schema = catalog.GetSchema(transaction, Identifier::DefaultSchema());
	auto entry = schema.GetEntry(transaction, CatalogType::SCALAR_FUNCTION_ENTRY,
	                              Identifier("DuckDB_pgRouting_Version"));
	if (!entry) {
		throw InternalException("pgrouting: DuckDB_pgRouting_Version was not registered");
	}
	entry->Cast<FunctionEntry>().tags.insert("ext", "pgrouting");
}

} // namespace duckdb
