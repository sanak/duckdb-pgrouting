// SPDX-License-Identifier: GPL-2.0-or-later

#include "routing/input_registry.hpp"

#include <cctype>

#include "duckdb/common/exception.hpp"

namespace duckdb_routing {

namespace {

struct ThreadState {
	duckdb::ClientContext *context = nullptr;
	InputRegistry *registry = nullptr;
	bool interrupted = false;
};

thread_local ThreadState state;

bool IEquals(const duckdb::string &a, const duckdb::string &b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (duckdb::idx_t i = 0; i < a.size(); i++) {
		if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
			return false;
		}
	}
	return true;
}

duckdb::idx_t PhysicalRow(const MaterializedInput &input, std::size_t row, int column) {
	auto &format = input.columns[duckdb::NumericCast<duckdb::idx_t>(column)];
	return format.sel->get_index(input.offset + duckdb::NumericCast<duckdb::idx_t>(row));
}

} // namespace

void InputRegistry::Register(const duckdb::string &sql, MaterializedInput input) {
	if (inputs.find(sql) != inputs.end()) {
		// The registry is keyed by the exact SQL string handed to the driver, and the edges and
		// combinations queries of a single call could be textually identical (or, in a future
		// family, any two of the queries this registry ever holds at once). Silently overwriting
		// the first registration would make the driver read the wrong rows for one of them.
		throw duckdb::InvalidInputException(
		    "routing: two inputs of the same call are registered under the same query text: %s", sql);
	}
	inputs[sql] = std::move(input);
}

const MaterializedInput *InputRegistry::Find(const duckdb::string &sql) const {
	auto it = inputs.find(sql);
	return it == inputs.end() ? nullptr : &it->second;
}

ScopedRoutingContext::ScopedRoutingContext(duckdb::ClientContext &context, InputRegistry &registry) {
	state.context = &context;
	state.registry = &registry;
	state.interrupted = false;
}

ScopedRoutingContext::~ScopedRoutingContext() {
	state.context = nullptr;
	state.registry = nullptr;
	state.interrupted = false;
}

const InputHandle &LookupInput(const std::string &sql) {
	if (!state.registry) {
		throw std::string("Internal error: no routing context is active");
	}
	auto *found = state.registry->Find(sql);
	if (!found) {
		// The driver asked for an input the DuckDB layer never registered.
		throw std::string("Internal error: no input registered for query: ") + sql;
	}
	return *found;
}

std::size_t InputRowCount(const InputHandle &handle) {
	return handle.count;
}

int FindColumn(const InputHandle &handle, const std::string &name) {
	for (duckdb::idx_t i = 0; i < handle.names.size(); i++) {
		if (IEquals(handle.names[i], name)) {
			return duckdb::NumericCast<int>(i);
		}
	}
	return -1;
}

ColumnClass ColumnClassOf(const InputHandle &handle, int column) {
	return handle.classes[duckdb::NumericCast<duckdb::idx_t>(column)];
}

bool IsNull(const InputHandle &handle, std::size_t row, int column) {
	auto &format = handle.columns[duckdb::NumericCast<duckdb::idx_t>(column)];
	return !format.validity.RowIsValid(PhysicalRow(handle, row, column));
}

int64_t ReadInt64(const InputHandle &handle, std::size_t row, int column) {
	auto &format = handle.columns[duckdb::NumericCast<duckdb::idx_t>(column)];
	const auto idx = PhysicalRow(handle, row, column);
	switch (handle.types[duckdb::NumericCast<duckdb::idx_t>(column)].id()) {
	case duckdb::LogicalTypeId::TINYINT:
		return duckdb::UnifiedVectorFormat::GetData<int8_t>(format)[idx];
	case duckdb::LogicalTypeId::SMALLINT:
		return duckdb::UnifiedVectorFormat::GetData<int16_t>(format)[idx];
	case duckdb::LogicalTypeId::INTEGER:
		return duckdb::UnifiedVectorFormat::GetData<int32_t>(format)[idx];
	case duckdb::LogicalTypeId::BIGINT:
		return duckdb::UnifiedVectorFormat::GetData<int64_t>(format)[idx];
	case duckdb::LogicalTypeId::UTINYINT:
		return duckdb::UnifiedVectorFormat::GetData<uint8_t>(format)[idx];
	case duckdb::LogicalTypeId::USMALLINT:
		return duckdb::UnifiedVectorFormat::GetData<uint16_t>(format)[idx];
	case duckdb::LogicalTypeId::UINTEGER:
		return duckdb::UnifiedVectorFormat::GetData<uint32_t>(format)[idx];
	default:
		throw std::string("Internal error: column is not ANY-INTEGER");
	}
}

double ReadDouble(const InputHandle &handle, std::size_t row, int column) {
	auto &format = handle.columns[duckdb::NumericCast<duckdb::idx_t>(column)];
	const auto idx = PhysicalRow(handle, row, column);
	switch (handle.types[duckdb::NumericCast<duckdb::idx_t>(column)].id()) {
	case duckdb::LogicalTypeId::FLOAT:
		return duckdb::UnifiedVectorFormat::GetData<float>(format)[idx];
	case duckdb::LogicalTypeId::DOUBLE:
		return duckdb::UnifiedVectorFormat::GetData<double>(format)[idx];
	default:
		// Only the integer classes are left: DECIMAL was cast to DOUBLE at unpack time.
		return static_cast<double>(ReadInt64(handle, row, column));
	}
}

char ReadChar(const InputHandle &handle, std::size_t row, int column) {
	const auto text = ReadText(handle, row, column);
	return text.empty() ? ' ' : text[0];
}

std::string ReadText(const InputHandle &handle, std::size_t row, int column) {
	auto &format = handle.columns[duckdb::NumericCast<duckdb::idx_t>(column)];
	const auto idx = PhysicalRow(handle, row, column);
	auto value = duckdb::UnifiedVectorFormat::GetData<duckdb::string_t>(format)[idx];
	return value.GetString();
}

std::vector<int64_t> ReadInt64Array(const InputHandle &, std::size_t, int) {
	// Only the restrictions and TRSP families need ANY-INTEGER-ARRAY columns, and none of them is
	// registered yet. Until one is, failing loudly beats returning something plausible.
	throw std::string("Internal error: ANY-INTEGER-ARRAY columns are not supported yet");
}

void CheckForInterrupts() {
	if (!state.context) {
		return;
	}
	try {
		state.context->InterruptCheck();
	} catch (...) {
		// pgRouting's drivers catch everything, so record the reason before unwinding.
		state.interrupted = true;
		throw;
	}
}

bool WasInterrupted() {
	return state.interrupted;
}

void ClearInterrupted() {
	state.interrupted = false;
}

} // namespace duckdb_routing
