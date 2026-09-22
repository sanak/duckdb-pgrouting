// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Completes the opaque PostgreSQL handles of postgres.h. Compiled only into pg_compat and
// pgRouting translation units.

#include <cstddef>
#include <cstdint>
#include <vector>

#include <postgres.h>

#include "pgrouting/input_access.hpp"

struct HeapTupleData {
	const duckdb_pgrouting::InputHandle *input;
	std::size_t row;
};

struct TupleDescData {
	const duckdb_pgrouting::InputHandle *input;
};

struct ArrayType {
	std::vector<int64_t> values;
};
