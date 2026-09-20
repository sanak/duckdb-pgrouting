// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Every public overload declared as data. Upstream names are kept verbatim so a spec row can be
// compared with pgRouting's own SQL signature files character by character; the public name is
// derived by stripping the pgr_ prefix.

#include <cstdint>
#include <string>

#include "duckdb.hpp"

namespace duckdb_routing {

// What a positional argument of a public function means.
enum class ArgKind : uint8_t {
	EDGES_SQL,        // VARCHAR
	COMBINATIONS_SQL, // VARCHAR
	START_VID,        // BIGINT
	END_VID,          // BIGINT
	START_VIDS,       // BIGINT[]
	END_VIDS,         // BIGINT[]
	DIRECTED          // BOOLEAN, only in the variant that takes it positionally
};

// The driver flags that are fixed per overload rather than chosen by the caller.
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
	DriverFlags flags;
};

// "pgr_dijkstra" -> "dijkstra". The prefix is the single naming rule of this extension.
inline duckdb::string PublicName(const char *upstream_name) {
	duckdb::string name(upstream_name);
	const duckdb::string prefix = "pgr_";
	return name.rfind(prefix, 0) == 0 ? name.substr(prefix.size()) : name;
}

// pgr_dijkstra's five signatures. normal = false on many-to-one is how upstream reverses the
// graph: fetch_edge swaps source and target when normal is false, and post_process reverses the
// resulting paths. See third_party/pgrouting/sql/dijkstra/dijkstra.sql.
extern const duckdb::vector<FunctionSpec> SHORTEST_PATH_SPECS;

} // namespace duckdb_routing
