// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Every public overload declared as data. Upstream names are kept verbatim so a spec row can be
// compared with pgRouting's own SQL signature files character by character; the public name is
// derived by stripping the pgr_ prefix.

#include <cstdint>
#include <string>

#include "duckdb.hpp"

namespace duckdb_routing {

// What a required positional argument of a public function means.
enum class ArgKind : uint8_t {
	EDGES_SQL,        // VARCHAR
	COMBINATIONS_SQL, // VARCHAR
	START_VID,        // BIGINT
	END_VID,          // BIGINT
	START_VIDS,       // BIGINT[]
	END_VIDS          // BIGINT[]
};

enum class OptionalType : uint8_t { BOOLEAN, BIGINT };

// A parameter upstream declares with a DEFAULT. PostgreSQL accepts any leading run of these
// positionally as well as by name; DuckDB never matches a named parameter positionally, so a spec
// row with k of them is registered as k + 1 variants, taking the first 0..k positionally. Kept as
// plain data (no LogicalType or Value) so the spec table needs no dynamic initialization that
// would depend on DuckDB's own static objects.
struct OptionalParam {
	const char *name;
	OptionalType type;
	int64_t default_value; // 0 or 1 for BOOLEAN
};

constexpr OptionalParam DIRECTED {"directed", OptionalType::BOOLEAN, 1};
constexpr OptionalParam CAP {"cap", OptionalType::BIGINT, 1};
constexpr OptionalParam GLOBAL {"global", OptionalType::BOOLEAN, 1};

// The driver flags that are fixed per overload rather than chosen by the caller. n_goals and
// global are only fallbacks: an overload that declares `cap` or `global` takes them from the call.
struct DriverFlags {
	bool only_cost = false;
	bool normal = true;
	int64_t n_goals = 0;
	bool global = false;
	char driving_side = ' ';
	bool details = true;
	int32_t which = 0;
	const char *result_kind = "path";
};

struct FunctionSpec {
	const char *upstream_name;
	duckdb::vector<ArgKind> args;
	duckdb::vector<OptionalParam> optionals; // upstream's declaration order
	DriverFlags flags;
};

// "pgr_dijkstra" -> "dijkstra". The prefix is the single naming rule of this extension.
inline duckdb::string PublicName(const char *upstream_name) {
	duckdb::string name(upstream_name);
	const duckdb::string prefix = "pgr_";
	return name.rfind(prefix, 0) == 0 ? name.substr(prefix.size()) : name;
}

// Defined in shortest_path_specs.cpp.
extern const duckdb::vector<FunctionSpec> SHORTEST_PATH_SPECS;

} // namespace duckdb_routing
