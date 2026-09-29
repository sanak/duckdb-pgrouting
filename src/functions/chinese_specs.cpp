// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of sql/chinese/*.sql: pgr_chinesePostman selects `seq, node, edge, cost,
// agg_cost` of _pgr_chinesePostman(edges, only_cost := false), and pgr_chinesePostmanCost the cost of
// its only_cost row (RETURNS FLOAT, so the column is the function's lower-case name).

#include "function_spec.hpp"

namespace duckdb_pgrouting {

namespace {

DriverFlags PostmanCostFlags() {
	auto flags = ColumnsFlags(DriverKind::CHINESE_POSTMAN, "cost AS pgr_chinesepostmancost");
	flags.only_cost = true;
	return flags;
}

} // namespace

const duckdb::vector<FunctionSpec> CHINESE_SPECS = {
    {"pgr_chinesePostman", {ArgKind::EDGES_SQL}, {},
     ColumnsFlags(DriverKind::CHINESE_POSTMAN, "seq, node, edge, cost, agg_cost")},
    {"pgr_chinesePostmanCost", {ArgKind::EDGES_SQL}, {}, PostmanCostFlags(), "pgr_chinesepostmancost"},
};

} // namespace duckdb_pgrouting
