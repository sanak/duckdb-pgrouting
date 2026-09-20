// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Shadows third_party/pgrouting/include/cpp_common/get_data.hpp. Upstream opens an SPI cursor
// over the SQL string; here the string is only a key into the registry of inputs that the DuckDB
// layer already materialized.

#include <cstdint>
#include <string>
#include <vector>

#include "cpp_common/info_t.hpp"
#include "cpp_common/get_check_data.hpp"
#include "routing/pg_types.hpp"

namespace pgrouting {
namespace pgget {

template <typename Data_type, typename Func>
std::vector<Data_type> get_data(const std::string &sql, bool flag, std::vector<Column_info_t> info, Func func) {
	const auto &input = duckdb_routing::LookupInput(sql);
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
