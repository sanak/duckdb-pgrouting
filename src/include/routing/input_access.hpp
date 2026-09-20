// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// The only header shared by the PostgreSQL-compatibility world and the DuckDB world.
// It must include neither DuckDB headers nor the compat postgres.h: the compat header defines
// ERROR as a macro, and DuckDB has an enumerator of that name, so the two never meet in one
// translation unit.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace duckdb_routing {

// One materialized SQL input. Declared here, defined on the DuckDB side.
struct InputHandle;

// The class of a column, as pgRouting's expectType sees it.
enum class ColumnClass : uint8_t { INTEGER, NUMERIC, TEXT, CHAR1, INTEGER_ARRAY, UNSUPPORTED };

// Looks up the input registered for this exact SQL string.
// Throws std::string when the key is unknown: that means the driver asked for an input the
// DuckDB layer did not provide, which is a bug in this extension.
const InputHandle &LookupInput(const std::string &sql);

std::size_t InputRowCount(const InputHandle &input);

// -1 when no column of that name exists. Matching is case-insensitive.
int FindColumn(const InputHandle &input, const std::string &name);
ColumnClass ColumnClassOf(const InputHandle &input, int column);

bool IsNull(const InputHandle &input, std::size_t row, int column);
int64_t ReadInt64(const InputHandle &input, std::size_t row, int column);
double ReadDouble(const InputHandle &input, std::size_t row, int column);
char ReadChar(const InputHandle &input, std::size_t row, int column);
std::string ReadText(const InputHandle &input, std::size_t row, int column);
std::vector<int64_t> ReadInt64Array(const InputHandle &input, std::size_t row, int column);

// Raised by the driver's CHECK_FOR_INTERRUPTS(). Sets a thread-local flag, then throws, because
// pgRouting's drivers catch every exception and the flag is how the exec layer learns why.
void CheckForInterrupts();
bool WasInterrupted();
void ClearInterrupted();

} // namespace duckdb_routing
