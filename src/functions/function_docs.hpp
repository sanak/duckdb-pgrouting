// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "duckdb.hpp"
#include "duckdb/parser/parsed_data/create_function_info.hpp"

namespace duckdb_pgrouting {

// The catalog description and example of the public function `name` (its upstream name), as
// reported by duckdb_functions(). Throws InternalException when function_docs.cpp has no row for
// it, so a function cannot be registered without one.
duckdb::FunctionDescription DescriptionOf(const duckdb::string &name);

} // namespace duckdb_pgrouting
