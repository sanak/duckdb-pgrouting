// SPDX-License-Identifier: GPL-2.0-or-later

// Link-time replacement for pgRouting's get_check_data.cpp. Upstream reads PostgreSQL tuples
// through heap_getattr and the type cache; here every value comes from a DuckDB vector, reached
// through input_access.hpp. Error texts are upstream's, character for character, because they
// are part of the documented behaviour.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "pgrouting/pg_types.hpp"
#include "cpp_common/alloc.hpp"
#include "cpp_common/get_check_data.hpp"
#include "cpp_common/info_t.hpp"

namespace pgrouting {

bool column_found(int colNumber) {
	return colNumber != -1;
}

namespace {

bool Accepts(expectType expected, duckdb_pgrouting::ColumnClass actual) {
	using duckdb_pgrouting::ColumnClass;
	switch (expected) {
	case ANY_INTEGER:
		return actual == ColumnClass::INTEGER;
	case ANY_NUMERICAL:
		return actual == ColumnClass::INTEGER || actual == ColumnClass::NUMERIC;
	case TEXT:
		return actual == ColumnClass::TEXT;
	case CHAR1:
		return actual == ColumnClass::CHAR1 || actual == ColumnClass::TEXT;
	case ANY_INTEGER_ARRAY:
		return actual == ColumnClass::INTEGER_ARRAY;
	}
	return false;
}

const char *ExpectedName(expectType expected) {
	switch (expected) {
	case ANY_INTEGER:
		return "ANY-INTEGER";
	case ANY_NUMERICAL:
		return "ANY-NUMERICAL";
	case TEXT:
		return "TEXT";
	case CHAR1:
		return "TEXT";
	case ANY_INTEGER_ARRAY:
		return "ANY-INTEGER-ARRAY";
	}
	return "UNKNOWN";
}

} // namespace

void fetch_column_info(const TupleDesc &tupdesc, std::vector<Column_info_t> &info) {
	for (auto &col : info) {
		col.colNumber = duckdb_pgrouting::FindColumn(*tupdesc->input, col.name);
		if (col.colNumber == -1) {
			if (col.strict) {
				throw std::string("Column '") + col.name + "' not Found";
			}
			continue;
		}
		const auto actual = duckdb_pgrouting::ColumnClassOf(*tupdesc->input, col.colNumber);
		if (!Accepts(col.eType, actual)) {
			throw std::string("Unexpected Column '") + col.name + "' type. Expected " + ExpectedName(col.eType);
		}
	}
}

int64_t getBigInt(const HeapTuple tuple, const TupleDesc &, const Column_info_t &info) {
	if (duckdb_pgrouting::IsNull(*tuple->input, tuple->row, info.colNumber)) {
		throw std::string("Unexpected Null value in column ") + info.name;
	}
	return duckdb_pgrouting::ReadInt64(*tuple->input, tuple->row, info.colNumber);
}

double getFloat8(const HeapTuple tuple, const TupleDesc &, const Column_info_t &info) {
	if (duckdb_pgrouting::IsNull(*tuple->input, tuple->row, info.colNumber)) {
		throw std::string("Unexpected Null value in column ") + info.name;
	}
	return duckdb_pgrouting::ReadDouble(*tuple->input, tuple->row, info.colNumber);
}

char getChar(const HeapTuple tuple, const TupleDesc &, const Column_info_t &info, bool strict, char default_value) {
	if (duckdb_pgrouting::IsNull(*tuple->input, tuple->row, info.colNumber)) {
		if (strict) {
			throw std::string("Unexpected Null value in column ") + info.name;
		}
		return default_value;
	}
	return duckdb_pgrouting::ReadChar(*tuple->input, tuple->row, info.colNumber);
}

char *getText(const HeapTuple tuple, const TupleDesc &, const Column_info_t &info) {
	if (duckdb_pgrouting::IsNull(*tuple->input, tuple->row, info.colNumber)) {
		throw std::string("Unexpected Null value in column ") + info.name;
	}
	return to_pg_msg(duckdb_pgrouting::ReadText(*tuple->input, tuple->row, info.colNumber));
}

int64_t *getBigIntArr(const HeapTuple tuple, const TupleDesc &, const Column_info_t &info, size_t *size) {
	const auto values = duckdb_pgrouting::ReadInt64Array(*tuple->input, tuple->row, info.colNumber);
	*size = values.size();
	auto *out = pgr_alloc(values.size(), static_cast<int64_t *>(nullptr));
	std::copy(values.begin(), values.end(), out);
	return out;
}

std::set<int64_t> get_pgset(ArrayType *v) {
	std::set<int64_t> result;
	if (v) {
		result.insert(v->values.begin(), v->values.end());
	}
	return result;
}

std::vector<int64_t> get_pgarray(ArrayType *v, bool allow_empty) {
	if (!v) {
		return {};
	}
	if (v->values.empty() && !allow_empty) {
		throw std::string("No elements found");
	}
	return v->values;
}

int64_t *get_array(ArrayType *v, size_t *size, bool allow_empty) {
	const auto values = get_pgarray(v, allow_empty);
	*size = values.size();
	auto *out = pgr_alloc(values.size(), static_cast<int64_t *>(nullptr));
	std::copy(values.begin(), values.end(), out);
	return out;
}

} // namespace pgrouting
