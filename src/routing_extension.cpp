// SPDX-License-Identifier: GPL-2.0-or-later
#define DUCKDB_EXTENSION_MAIN

#include "routing_extension.hpp"

#include "duckdb.hpp"
#include "routing/register.hpp"

namespace duckdb {

static void LoadInternal(ExtensionLoader &loader) {
	RegisterMetaFunctions(loader);
	RegisterShortestPathExec(loader);
	RegisterShortestPathFunctions(loader);
}

void RoutingExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}

std::string RoutingExtension::Name() {
	return "routing";
}

std::string RoutingExtension::Version() const {
#ifdef EXT_VERSION_ROUTING
	return EXT_VERSION_ROUTING;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(routing, loader) {
	duckdb::LoadInternal(loader);
}
}
