// SPDX-License-Identifier: GPL-2.0-or-later

// One description and one example per public function, read back through
// duckdb_functions().description / .examples. The wording is this project's own: pgRouting's
// documentation is CC-BY-SA and is never copied here. Every example runs on upstream's sample
// graph (the tables loaded from test/data/sampledata/), and
// scripts/tests/test_catalog_examples.py executes each one.

#include "function_docs.hpp"

#include "duckdb/common/exception.hpp"

namespace duckdb_pgrouting {

namespace {

// Plain data, so the table needs no dynamic initialization (as in shortest_path_specs.cpp).
struct FunctionDoc {
	const char *name;
	const char *description;
	const char *example;
};

const FunctionDoc FUNCTION_DOCS[] = {
    {"pgr_dijkstra",
     "Shortest paths by Dijkstra's algorithm, one row per step of each path; takes one or many start "
     "and end vertices, or a query of start/end pairs.",
     "SELECT * FROM pgr_dijkstra('SELECT id, source, target, cost, reverse_cost FROM edges', 6, 10)"},
    {"pgr_dijkstraCost",
     "Total cost of the Dijkstra shortest path for each requested start/end pair, without the path.",
     "SELECT * FROM pgr_dijkstraCost('SELECT id, source, target, cost, reverse_cost FROM edges', 6, 10)"},
    {"pgr_dijkstraCostMatrix",
     "Dijkstra shortest-path costs between every ordered pair of the given vertices, one row per pair.",
     "SELECT * FROM pgr_dijkstraCostMatrix('SELECT id, source, target, cost, reverse_cost FROM edges', "
     "[5, 6, 10, 15])"},
    {"pgr_dijkstraNear",
     "Dijkstra paths that stop at the nearest of several destinations (or start from the nearest of "
     "several origins), keeping at most cap paths.",
     "SELECT * FROM pgr_dijkstraNear('SELECT id, source, target, cost, reverse_cost FROM edges', 6, "
     "[10, 11, 1])"},
    {"pgr_dijkstraNearCost",
     "Total cost of each path pgr_dijkstraNear keeps, one row per path.",
     "SELECT * FROM pgr_dijkstraNearCost('SELECT id, source, target, cost, reverse_cost FROM edges', 6, "
     "[10, 11, 1])"},
    {"pgr_withPoints",
     "Dijkstra shortest paths on the graph plus temporary points placed along its edges; a point is "
     "addressed by the negative of its id.",
     "SELECT * FROM pgr_withPoints('SELECT id, source, target, cost, reverse_cost FROM edges', "
     "'SELECT pid, edge_id, fraction, side FROM pointsofinterest', -1, 10)"},
    {"pgr_withPointsCost",
     "Total cost of each shortest path on the graph plus temporary points placed along its edges.",
     "SELECT * FROM pgr_withPointsCost('SELECT id, source, target, cost, reverse_cost FROM edges', "
     "'SELECT pid, edge_id, fraction, side FROM pointsofinterest', -1, 10)"},
    {"pgr_withPointsCostMatrix",
     "Shortest-path costs between every ordered pair of the given vertices and points, one row per "
     "pair.",
     "SELECT * FROM pgr_withPointsCostMatrix('SELECT id, source, target, cost, reverse_cost FROM edges', "
     "'SELECT pid, edge_id, fraction, side FROM pointsofinterest', [-1, 7, 10])"},
    {"pgr_bdDijkstra",
     "Shortest paths by a bidirectional Dijkstra search, which grows from both ends until they meet.",
     "SELECT * FROM pgr_bdDijkstra('SELECT id, source, target, cost, reverse_cost FROM edges', 6, 10)"},
    {"pgr_bdDijkstraCost",
     "Total cost of the bidirectional Dijkstra shortest path for each requested start/end pair.",
     "SELECT * FROM pgr_bdDijkstraCost('SELECT id, source, target, cost, reverse_cost FROM edges', 6, 10)"},
    {"pgr_bdDijkstraCostMatrix",
     "Bidirectional Dijkstra costs between every ordered pair of the given vertices, one row per pair.",
     "SELECT * FROM pgr_bdDijkstraCostMatrix('SELECT id, source, target, cost, reverse_cost FROM edges', "
     "[5, 6, 10, 15])"},
    {"pgr_bellmanFord",
     "Shortest paths by the Bellman-Ford algorithm, one row per step of each path.",
     "SELECT * FROM pgr_bellmanFord('SELECT id, source, target, cost, reverse_cost FROM edges', 6, 10)"},
    {"pgr_edwardMoore",
     "Shortest paths by the Edward Moore algorithm, a queue-based refinement of Bellman-Ford.",
     "SELECT * FROM pgr_edwardMoore('SELECT id, source, target, cost, reverse_cost FROM edges', 6, 10)"},
    {"pgr_dagShortestPath",
     "Shortest paths on a directed acyclic graph, found in a single pass over a topological order; "
     "fails when the graph has a cycle.",
     "SELECT * FROM pgr_dagShortestPath('SELECT id, source, target, cost FROM edges', 5, 11)"},
    {"pgr_binaryBreadthFirstSearch",
     "Shortest paths on a graph with at most two distinct edge costs, one of them zero, by a "
     "breadth-first search over a double-ended queue.",
     "SELECT * FROM pgr_binaryBreadthFirstSearch('SELECT id, source, target, cost, reverse_cost FROM "
     "edges', 6, 10)"},
    {"pgr_version", "Version of the pgRouting library built into this extension.", "SELECT pgr_version()"},
};

} // namespace

duckdb::FunctionDescription DescriptionOf(const duckdb::string &name) {
	for (const auto &doc : FUNCTION_DOCS) {
		if (name == doc.name) {
			duckdb::FunctionDescription description;
			description.description = doc.description;
			description.examples.push_back(doc.example);
			return description;
		}
	}
	throw duckdb::InternalException("pgrouting: no catalog description for %s", name);
}

} // namespace duckdb_pgrouting
