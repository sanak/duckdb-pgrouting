// SPDX-License-Identifier: GPL-2.0-or-later

#include "routing/register.hpp"

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
}

} // namespace duckdb
