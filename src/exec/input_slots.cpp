// SPDX-License-Identifier: GPL-2.0-or-later

#include "pgrouting/input_slots.hpp"

#include <vector>

#include "duckdb/common/exception.hpp"
#include "duckdb/common/types/vector.hpp"
#include "duckdb/common/vector_operations/vector_operations.hpp"

#include "pgrouting/withpoints_keys.hpp"

namespace duckdb {

namespace {

using duckdb_pgrouting::DriverRequest;

enum class SlotKind : uint8_t { ROW_LIST, ID_LIST };

// One column of the input row. A row list is registered under `registry_kind` and the SQL text
// `registry_key` returns, which is the text the driver looks it up by: for the two derived withPoints
// inputs that is a query the driver builds, not one the caller wrote. An id list is copied into
// `ids`, and `has_ids` records that its cell was not NULL. std::vector, not DuckDB's vector: these
// point into DriverRequest, which includes no DuckDB header.
struct InputSlot {
	const char *column;
	SlotKind kind;
	bool required;
	const char *registry_kind;
	std::string (*registry_key)(const DriverRequest &);
	std::vector<int64_t> DriverRequest::*ids;
	bool DriverRequest::*has_ids;
};

std::string EdgesKey(const DriverRequest &request) {
	return request.edges_sql;
}
std::string CombinationsKey(const DriverRequest &request) {
	return request.combinations_sql;
}
std::string PointsKey(const DriverRequest &request) {
	return request.points_sql;
}
std::string EdgesOfPointsKey(const DriverRequest &request) {
	return duckdb_pgrouting::WithPointsDerivedKeys(request.edges_sql, request.points_sql).of_points;
}
std::string EdgesNoPointsKey(const DriverRequest &request) {
	return duckdb_pgrouting::WithPointsDerivedKeys(request.edges_sql, request.points_sql).no_points;
}
std::string RestrictionsKey(const DriverRequest &request) {
	return request.restrictions_sql;
}
std::string MatrixKey(const DriverRequest &request) {
	return request.matrix_sql;
}
std::string CoordinatesKey(const DriverRequest &request) {
	return request.coordinates_sql;
}

// With points given, the public overloads pass 'edges' as an untyped NULL: the driver then fetches
// the points query and the two edge queries it derives from edges_sql and points_sql, never
// edges_sql itself.
const InputSlot INPUT_SLOTS[] = {
    {"edges", SlotKind::ROW_LIST, true, duckdb_pgrouting::KIND_EDGES, EdgesKey, nullptr, nullptr},
    {"combinations", SlotKind::ROW_LIST, false, duckdb_pgrouting::KIND_COMBINATIONS, CombinationsKey, nullptr,
     nullptr},
    {"points", SlotKind::ROW_LIST, false, duckdb_pgrouting::KIND_POINTS, PointsKey, nullptr, nullptr},
    {"edges_of_points", SlotKind::ROW_LIST, false, duckdb_pgrouting::KIND_EDGES, EdgesOfPointsKey, nullptr,
     nullptr},
    {"edges_no_points", SlotKind::ROW_LIST, false, duckdb_pgrouting::KIND_EDGES, EdgesNoPointsKey, nullptr,
     nullptr},
    {"restrictions", SlotKind::ROW_LIST, false, duckdb_pgrouting::KIND_RESTRICTIONS, RestrictionsKey, nullptr,
     nullptr},
    {"matrix", SlotKind::ROW_LIST, false, duckdb_pgrouting::KIND_MATRIX, MatrixKey, nullptr, nullptr},
    {"coordinates", SlotKind::ROW_LIST, false, duckdb_pgrouting::KIND_COORDINATES, CoordinatesKey, nullptr,
     nullptr},
    {"starts", SlotKind::ID_LIST, false, nullptr, nullptr, &DriverRequest::starts, &DriverRequest::has_starts},
    {"ends", SlotKind::ID_LIST, false, nullptr, nullptr, &DriverRequest::ends, &DriverRequest::has_ends},
    {"roots", SlotKind::ID_LIST, false, nullptr, nullptr, &DriverRequest::roots, &DriverRequest::has_roots},
    {"via", SlotKind::ID_LIST, false, nullptr, nullptr, &DriverRequest::via, &DriverRequest::has_via},
    {"methods", SlotKind::ID_LIST, false, nullptr, nullptr, &DriverRequest::methods, &DriverRequest::has_methods},
    {"forbidden", SlotKind::ID_LIST, false, nullptr, nullptr, &DriverRequest::forbidden,
     &DriverRequest::has_forbidden},
};

duckdb_pgrouting::ColumnClass ClassOf(const LogicalType &type) {
	using duckdb_pgrouting::ColumnClass;
	switch (type.id()) {
	case LogicalTypeId::TINYINT:
	case LogicalTypeId::SMALLINT:
	case LogicalTypeId::INTEGER:
	case LogicalTypeId::BIGINT:
	case LogicalTypeId::UTINYINT:
	case LogicalTypeId::USMALLINT:
	case LogicalTypeId::UINTEGER:
		// UBIGINT is deliberately absent: it does not round-trip through int64_t.
		return ColumnClass::INTEGER;
	case LogicalTypeId::FLOAT:
	case LogicalTypeId::DOUBLE:
	case LogicalTypeId::DECIMAL:
		return ColumnClass::NUMERIC;
	case LogicalTypeId::VARCHAR:
		return ColumnClass::TEXT;
	case LogicalTypeId::LIST:
		// ANY-INTEGER-ARRAY: a list of any type ANY-INTEGER accepts (a literal [7, 10] is INTEGER[]).
		return ClassOf(ListType::GetChildType(type)) == ColumnClass::INTEGER ? ColumnClass::INTEGER_ARRAY
		                                                                    : ColumnClass::UNSUPPORTED;
	default:
		return ColumnClass::UNSUPPORTED;
	}
}

// Unpacks one LIST(STRUCT) cell into per-child vectors. Returns false when the cell is NULL.
bool Unpack(ClientContext &context, Vector &list_column, idx_t count, idx_t row,
            duckdb_pgrouting::MaterializedInput &out) {
	UnifiedVectorFormat list_format;
	list_column.ToUnifiedFormat(count, list_format);
	const auto list_idx = list_format.sel->get_index(row);
	if (!list_format.validity.RowIsValid(list_idx)) {
		return false; // NULL input
	}
	const auto entry = UnifiedVectorFormat::GetData<list_entry_t>(list_format)[list_idx];
	auto &child = ListVector::GetEntry(list_column);
	const auto child_count = ListVector::GetListSize(list_column);
	auto &struct_children = StructVector::GetEntries(child);
	auto &struct_type = ListType::GetChildType(list_column.GetType());

	out.offset = entry.offset;
	out.count = entry.length;
	out.columns.resize(struct_children.size());
	out.list_children.resize(struct_children.size());
	for (idx_t c = 0; c < struct_children.size(); c++) {
		out.names.push_back(StructType::GetChildName(struct_type, c));
		auto *source = struct_children[c].get();
		if (source->GetType().id() == LogicalTypeId::DECIMAL) {
			// pgRouting's ANY-NUMERICAL includes DECIMAL, but reading a DECIMAL cell means
			// knowing its scale and physical width. Cast the whole column once instead.
			auto casted = make_uniq<Vector>(LogicalType::DOUBLE, child_count);
			VectorOperations::Cast(context, *source, *casted, child_count);
			out.owned.push_back(std::move(casted));
			source = out.owned.back().get();
		}
		if (ClassOf(source->GetType()) == duckdb_pgrouting::ColumnClass::INTEGER_ARRAY) {
			// Every integer list is cast to BIGINT[] once, as DECIMAL is above, so ReadInt64Array reads
			// one element type; its elements are kept beside the list's own offsets.
			if (ListType::GetChildType(source->GetType()).id() != LogicalTypeId::BIGINT) {
				auto casted = make_uniq<Vector>(LogicalType::LIST(LogicalType::BIGINT), child_count);
				VectorOperations::Cast(context, *source, *casted, child_count);
				out.owned.push_back(std::move(casted));
				source = out.owned.back().get();
			}
			auto &elements = ListVector::GetEntry(*source);
			elements.ToUnifiedFormat(ListVector::GetListSize(*source), out.list_children[c]);
		}
		out.types.push_back(source->GetType());
		out.classes.push_back(ClassOf(source->GetType()));
		source->ToUnifiedFormat(child_count, out.columns[c]);
	}
	return true;
}

// Builds a registrable input that has the right columns and no rows. pgRouting's fetchers resolve
// and type-check the column names before reading any row (fetch_column_info), and never index the
// per-column vectors when the row count is zero, so `columns` is deliberately left empty.
duckdb_pgrouting::MaterializedInput EmptyInput(const RowSchema &schema) {
	duckdb_pgrouting::MaterializedInput input;
	input.names = schema.names;
	input.types = schema.types;
	for (auto &type : schema.types) {
		// Unpack casts a DECIMAL child to DOUBLE and reports DOUBLE here; both land in
		// ColumnClass::NUMERIC, so the class a zero-row input reports is the same either way.
		input.classes.push_back(ClassOf(type));
	}
	return input;
}

// `name` is the column this list came from, needed only to name it in the exception below.
std::vector<int64_t> ReadIdList(DataChunk &input, idx_t column, const char *name, bool &present) {
	std::vector<int64_t> ids;
	present = false;
	const auto value = input.GetValue(column, 0);
	if (value.IsNull()) {
		return ids;
	}
	present = true;
	for (auto &child : ListValue::GetChildren(value)) {
		if (child.IsNull()) {
			// A NULL element inside an otherwise well-typed LIST(BIGINT) is reachable from the
			// public API too (pgr_dijkstra(sql, [1, NULL]::BIGINT[], 3)), not only from a direct
			// call. BigIntValue::Get on a NULL Value does not assert -- the Value still carries the
			// BIGINT physical type, only its payload is unset -- so it would silently read whatever
			// bytes happen to sit in that union and use them as a vertex id. PostgreSQL and
			// pgRouting reject a NULL array element outright; match that instead of returning an
			// answer that depends on uninitialized memory.
			throw InvalidInputException("_pgr_exec: column '%s' contains a NULL id", name);
		}
		ids.push_back(BigIntValue::Get(child));
	}
	return ids;
}

idx_t FindInputColumn(TableFunctionBindInput &input, const char *name) {
	for (idx_t i = 0; i < input.input_table_names.size(); i++) {
		if (input.input_table_names[i] == name) {
			return i;
		}
	}
	return DConstants::INVALID_INDEX;
}

// Spells the child names exactly as Unpack does, so that a zero-row input answers FindColumn the
// same way a populated one would.
void CaptureRowSchema(const LogicalType &type, RowSchema &schema) {
	auto &struct_type = ListType::GetChildType(type);
	for (idx_t c = 0; c < StructType::GetChildCount(struct_type); c++) {
		schema.names.push_back(StructType::GetChildName(struct_type, c));
		schema.types.push_back(StructType::GetChildType(struct_type, c));
	}
	schema.known = true;
}

} // namespace

bool IsIdListSlot(const string &column) {
	for (const auto &slot : INPUT_SLOTS) {
		if (slot.kind == SlotKind::ID_LIST && column == slot.column) {
			return true;
		}
	}
	return false;
}

// _pgr_exec is catalogued and callable by any user, not only through the public overloads' bind_replace. Unpack
// and ReadIdList reach ListVector::GetEntry / StructVector::GetEntries /
// ListValue::GetChildren / BigIntValue::Get, all of which raise InternalException (via D_ASSERT) on
// a type mismatch -- a class that invalidates the whole database instance. Checking each column's
// shape once, here at bind time, turns a wrong-typed argument into an ordinary user-input error
// instead, before any cell is ever read.
//
// An SQLNULL-typed column is exempt: every value in it is NULL by construction, and both Unpack and
// ReadIdList return early on a NULL cell without touching a LIST/STRUCT accessor. The public
// overloads rely on this: they pass an untyped NULL for 'edges'/'combinations' whenever the
// overload does not use them.
BoundSlots BindInputSlots(TableFunctionBindInput &input) {
	BoundSlots bound;
	for (const auto &slot : INPUT_SLOTS) {
		BoundSlot entry;
		entry.column = FindInputColumn(input, slot.column);
		if (entry.column == DConstants::INVALID_INDEX) {
			if (slot.required) {
				throw InvalidInputException("_pgr_exec: the input table has no '%s' column", slot.column);
			}
			bound.push_back(std::move(entry));
			continue;
		}
		const auto &type = input.input_table_types[entry.column];
		if (type.id() != LogicalTypeId::SQLNULL) {
			if (slot.kind == SlotKind::ROW_LIST) {
				if (type.id() != LogicalTypeId::LIST || ListType::GetChildType(type).id() != LogicalTypeId::STRUCT) {
					throw InvalidInputException("_pgr_exec: column '%s' must be LIST(STRUCT), got %s", slot.column,
					                            type.ToString());
				}
				CaptureRowSchema(type, entry.schema);
			} else if (type != LogicalType::LIST(LogicalType::BIGINT)) {
				// Exactly BIGINT[]: ReadIdList reads each element as a BIGINT Value. ClassOf's
				// ANY-INTEGER-ARRAY is wider, for restriction paths.
				throw InvalidInputException("_pgr_exec: column '%s' must be LIST(BIGINT), got %s", slot.column,
				                            type.ToString());
			}
		}
		bound.push_back(std::move(entry));
	}
	return bound;
}

// An input query that matches no rows makes list(<row>) evaluate to NULL; pgRouting still resolves
// the SQL string in the registry first and then reads zero rows, so the bound row shape is
// registered with no rows. A column that is absent, or has no bound row shape, is an input this
// call does not use; the driver never asks for it, so it stays unregistered.
void MaterializeInputSlots(ClientContext &context, const BoundSlots &slots, DataChunk &input,
                           duckdb_pgrouting::DriverRequest &request, duckdb_pgrouting::InputRegistry &registry) {
	for (idx_t i = 0; i < slots.size(); i++) {
		const auto &slot = INPUT_SLOTS[i];
		const auto &bound = slots[i];
		if (bound.column == DConstants::INVALID_INDEX) {
			continue;
		}
		if (slot.kind == SlotKind::ID_LIST) {
			request.*slot.ids = ReadIdList(input, bound.column, slot.column, request.*slot.has_ids);
			continue;
		}
		duckdb_pgrouting::MaterializedInput rows;
		if (Unpack(context, input.data[bound.column], input.size(), 0, rows)) {
			registry.Register(slot.registry_key(request), slot.registry_kind, std::move(rows));
		} else if (bound.schema.known) {
			registry.Register(slot.registry_key(request), slot.registry_kind, EmptyInput(bound.schema));
		}
	}
}

} // namespace duckdb
