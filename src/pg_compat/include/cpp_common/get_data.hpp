// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Shadows third_party/pgrouting/include/cpp_common/get_data.hpp. Upstream opens an SPI cursor
// over the SQL string; here the string is only a key into the registry of inputs that the DuckDB
// layer already materialized.

#include <cstdint>
#include <string>
#include <vector>

#include "c_types/ii_t_rt.h"
#include "cpp_common/edge_t.hpp"
#include "cpp_common/get_check_data.hpp"
#include "cpp_common/info_t.hpp"
#include "cpp_common/point_on_edge_t.hpp"
#include "routing/pg_types.hpp"

namespace pgrouting {
namespace pgget {

namespace detail {

// Maps a pgRouting row type to the kind tag its SQL string was registered under (see
// routing/input_access.hpp: DuckDB-side registration and this lookup agree on the tag because
// both name it from the row shape, not from the SQL text). A type with no entry here is never
// actually fetched by anything this extension currently calls, so the empty default is dead code,
// not a latent bug; a new family that starts calling get_data<SomeType> for a genuinely new input
// kind adds a specialization here alongside the new KIND_* constant and DuckDB-side registration.
// This is a hand-written trait rather than typeid(Data_type).name(): duckdb/CMakeLists.txt has a
// DISABLE_RTTI option (-fno-rtti) that is off in this build but is documented as something "a
// dependency that cannot build this way can re-enable ... for its own targets", i.e. a real
// configuration this project could end up building under, and typeid() would stop compiling then.
template <typename Data_type> inline std::string InputKind() {
	return std::string();
}
template <> inline std::string InputKind<Edge_t>() {
	return duckdb_routing::KIND_EDGES;
}
template <> inline std::string InputKind<II_t_rt>() {
	return duckdb_routing::KIND_COMBINATIONS;
}
template <> inline std::string InputKind<Point_on_edge_t>() {
	return duckdb_routing::KIND_POINTS;
}

} // namespace detail

template <typename Data_type, typename Func>
std::vector<Data_type> get_data(const std::string &sql, bool flag, std::vector<Column_info_t> info, Func func) {
	const auto &input = duckdb_routing::LookupInput(sql, detail::InputKind<Data_type>());
	TupleDescData desc_data {&input};
	TupleDesc desc = &desc_data;
	fetch_column_info(desc, info);

	std::vector<Data_type> tuples;
	const auto rows = duckdb_routing::InputRowCount(input);
	tuples.reserve(rows);
	int64_t default_id = 0;
	std::size_t valid = 0;
	for (std::size_t r = 0; r < rows; ++r) {
		HeapTupleData row {&input, r};
		tuples.push_back(func(&row, desc, info, &default_id, &valid, flag));
	}
	return tuples;
}

} // namespace pgget
} // namespace pgrouting
