// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "duckdb.hpp"

namespace duckdb {
class ExtensionLoader;

void RegisterMetaFunctions(ExtensionLoader &loader);
void RegisterShortestPathExec(ExtensionLoader &loader);
void RegisterShortestPathFunctions(ExtensionLoader &loader);
void RegisterExtractVertices(ExtensionLoader &loader);
void RegisterFindCloseEdges(ExtensionLoader &loader);
} // namespace duckdb
