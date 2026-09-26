// SPDX-License-Identifier: GPL-2.0-or-later
#define DUCKDB_EXTENSION_MAIN

#include "pgrouting_extension.hpp"

#include "duckdb.hpp"
#include "pgrouting/register.hpp"

namespace duckdb {

static void LoadInternal(ExtensionLoader &loader) {
	RegisterMetaFunctions(loader);
	RegisterShortestPathExec(loader);
	RegisterShortestPathFunctions(loader);
	RegisterExtractVertices(loader);
}

void PgroutingExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}

std::string PgroutingExtension::Name() {
	return "pgrouting";
}

std::string PgroutingExtension::Version() const {
#ifdef EXT_VERSION_PGROUTING
	return EXT_VERSION_PGROUTING;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(pgrouting, loader) {
	duckdb::LoadInternal(loader);
}
}
