// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Every DriverRequest field that crosses from a public overload to _pgr_exec travels as one named
// parameter of _pgr_exec, declared once in request_params.cpp. Registering the parameters, reading
// them back into a request and writing a request out as arguments (the public overloads'
// bind_replace) walk that one declaration. A family that needs a new request field adds it to
// DriverRequest and one row there.

#include "duckdb.hpp"
#include "duckdb/common/named_parameter_map.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/parser/parsed_expression.hpp"

#include "pgrouting/driver_request.hpp"

namespace duckdb {

// Adds one named parameter per request field to `exec`.
void RegisterRequestParameters(TableFunction &exec);

// Overwrites each request field whose named parameter was given and is not NULL; the others keep
// DriverRequest's defaults. Throws InvalidInputException for an unknown driver name.
void ReadRequestParameters(const named_parameter_map_t &named, duckdb_pgrouting::DriverRequest &request);

// The request as `name := constant` arguments to _pgr_exec, one per field, whether or not the
// caller changed it.
vector<unique_ptr<ParsedExpression>> RequestArguments(const duckdb_pgrouting::DriverRequest &request);

// Sets the field whose parameter is `name` from `value`, cast as the parameter would be. Returns
// false when no parameter has that name.
bool SetRequestParameter(duckdb_pgrouting::DriverRequest &request, const string &name, const Value &value);

bool IsRequestParameter(const string &name);

} // namespace duckdb
