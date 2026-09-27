// SPDX-License-Identifier: GPL-2.0-or-later

// The overload table of sql/spanningTree/*.sql (pgr_randomSpanTree is not a public function in
// pgRouting 4.0). Every wrapper calls _pgr_kruskalv4 or _pgr_primv4 (edges, roots, suffix,
// max_depth, distance). The suffix picks the answer; a form passes -1 for the bound it does not
// use, which neither the driver nor the request check reads for that suffix, so here that bound
// keeps the request's default.

#include "function_spec.hpp"

namespace duckdb_pgrouting {

namespace {

DriverFlags TreeFlags(DriverKind driver, const char *suffix) {
	auto flags = FamilyFlags(driver, false, Projection::ALL);
	flags.mst_suffix = suffix;
	return flags;
}

// pgr_kruskal / pgr_prim: the whole forest as (edge, cost), from roots ARRAY[0].
DriverFlags ForestFlags(DriverKind driver) {
	auto flags = FamilyFlags(driver, false, Projection::EDGE_COST);
	flags.root_zero = true;
	return flags;
}

} // namespace

const duckdb::vector<FunctionSpec> SPANNING_TREE_SPECS = {
    {"pgr_kruskal", {ArgKind::EDGES_SQL}, {}, ForestFlags(DriverKind::KRUSKAL)},
    {"pgr_kruskalBFS", {ArgKind::EDGES_SQL, ArgKind::ROOT}, {MAX_DEPTH}, TreeFlags(DriverKind::KRUSKAL, "BFS")},
    {"pgr_kruskalBFS", {ArgKind::EDGES_SQL, ArgKind::ROOTS}, {MAX_DEPTH}, TreeFlags(DriverKind::KRUSKAL, "BFS")},
    {"pgr_kruskalDFS", {ArgKind::EDGES_SQL, ArgKind::ROOT}, {MAX_DEPTH}, TreeFlags(DriverKind::KRUSKAL, "DFS")},
    {"pgr_kruskalDFS", {ArgKind::EDGES_SQL, ArgKind::ROOTS}, {MAX_DEPTH}, TreeFlags(DriverKind::KRUSKAL, "DFS")},
    // Upstream overloads the distance as NUMERIC and FLOAT; both are DOUBLE here, so each pair is one row.
    {"pgr_kruskalDD", {ArgKind::EDGES_SQL, ArgKind::ROOT, ArgKind::DISTANCE}, {},
     TreeFlags(DriverKind::KRUSKAL, "DD")},
    {"pgr_kruskalDD", {ArgKind::EDGES_SQL, ArgKind::ROOTS, ArgKind::DISTANCE}, {},
     TreeFlags(DriverKind::KRUSKAL, "DD")},

    {"pgr_prim", {ArgKind::EDGES_SQL}, {}, ForestFlags(DriverKind::PRIM)},
    {"pgr_primBFS", {ArgKind::EDGES_SQL, ArgKind::ROOT}, {MAX_DEPTH}, TreeFlags(DriverKind::PRIM, "BFS")},
    {"pgr_primBFS", {ArgKind::EDGES_SQL, ArgKind::ROOTS}, {MAX_DEPTH}, TreeFlags(DriverKind::PRIM, "BFS")},
    {"pgr_primDFS", {ArgKind::EDGES_SQL, ArgKind::ROOT}, {MAX_DEPTH}, TreeFlags(DriverKind::PRIM, "DFS")},
    {"pgr_primDFS", {ArgKind::EDGES_SQL, ArgKind::ROOTS}, {MAX_DEPTH}, TreeFlags(DriverKind::PRIM, "DFS")},
    {"pgr_primDD", {ArgKind::EDGES_SQL, ArgKind::ROOT, ArgKind::DISTANCE}, {}, TreeFlags(DriverKind::PRIM, "DD")},
    {"pgr_primDD", {ArgKind::EDGES_SQL, ArgKind::ROOTS, ArgKind::DISTANCE}, {}, TreeFlags(DriverKind::PRIM, "DD")},
};

} // namespace duckdb_pgrouting
