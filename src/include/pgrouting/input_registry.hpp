// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "duckdb.hpp"
#include "pgrouting/input_access.hpp"

namespace duckdb_pgrouting {

// One LIST(STRUCT) column of the single input row, unpacked into per-child vectors so that no
// value is ever boxed as duckdb::Value during the driver's row loop.
//
// This is the definition of the InputHandle that input_access.hpp forward-declares: the compat
// layer sees only the opaque name, this side sees the fields. MaterializedInput is the name the
// DuckDB side uses; they are the same type, so no cast is ever needed between them.
struct InputHandle {
	duckdb::vector<duckdb::string> names;
	duckdb::vector<duckdb::LogicalType> types;
	duckdb::vector<ColumnClass> classes;
	duckdb::vector<duckdb::UnifiedVectorFormat> columns;
	// DECIMAL children are cast to DOUBLE once, at unpack time, and the cast vector is owned
	// here; `types` then reports DOUBLE for that column. Casting per cell would be the only
	// other way to honour spec-mandated DECIMAL support without boxing every value.
	duckdb::vector<duckdb::unique_ptr<duckdb::Vector>> owned;
	duckdb::idx_t offset = 0; // first row of this list inside the child vectors
	duckdb::idx_t count = 0;
};

using MaterializedInput = InputHandle;

class InputRegistry {
public:
	// The key is the SQL string that will be handed to the driver plus a kind tag (KIND_EDGES,
	// KIND_COMBINATIONS, ...; see input_access.hpp) distinguishing two registrations that share
	// the same SQL text but feed different pgRouting row shapes.
	void Register(const duckdb::string &sql, const duckdb::string &kind, MaterializedInput input);
	const MaterializedInput *Find(const duckdb::string &sql, const duckdb::string &kind) const;

private:
	duckdb::unordered_map<duckdb::string, MaterializedInput> inputs;
};

// Makes a registry and a ClientContext visible to the pg_compat layer for the duration of one
// driver call. Thread-local: one driver runs per in-out call, and concurrent queries are
// independent.
//
// The destructor restores whatever was installed before rather than clearing the slots, so that a
// nested scope on one thread would leave the enclosing one intact. No such nesting exists today --
// the TABLE argument is a scalar subquery, fully materialized before the in-out operator runs, so
// two driver calls cannot interleave on one thread -- and this is three members rather than a
// dependency on that staying true.
class ScopedRoutingContext {
public:
	ScopedRoutingContext(duckdb::ClientContext &context, InputRegistry &registry);
	~ScopedRoutingContext();
	ScopedRoutingContext(const ScopedRoutingContext &) = delete;
	ScopedRoutingContext &operator=(const ScopedRoutingContext &) = delete;

private:
	duckdb::ClientContext *saved_context;
	InputRegistry *saved_registry;
	bool saved_interrupted;
};

} // namespace duckdb_pgrouting
