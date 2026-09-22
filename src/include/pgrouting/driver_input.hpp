// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Lets the exec layer hand PostgreSQL-shaped integer arrays to the driver without ever including
// postgres.h (see input_access.hpp for why that matters).

#include <cstdint>
#include <vector>

struct ArrayType;

namespace duckdb_pgrouting {

class ScopedIntArray {
public:
	// Builds an ArrayType holding these values.
	explicit ScopedIntArray(const std::vector<int64_t> &values);
	// Builds nothing: get() returns nullptr, which the driver reads as "argument not given".
	ScopedIntArray();
	~ScopedIntArray();
	ScopedIntArray(const ScopedIntArray &) = delete;
	ScopedIntArray &operator=(const ScopedIntArray &) = delete;

	ArrayType *get() const {
		return ptr;
	}

private:
	ArrayType *ptr;
};

} // namespace duckdb_pgrouting
