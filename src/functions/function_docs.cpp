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

// A plain C array of string literals: it depends on no DuckDB statics, so it needs no dynamic
// initialization at load time (unlike the *_specs.cpp tables, which build duckdb::vector objects).
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
    {"pgr_aStar",
     "Shortest paths by A* search, which steers toward the destination using each edge's end-point "
     "coordinates (x1, y1, x2, y2); takes one or many start and end vertices, or a query of start/end "
     "pairs.",
     "SELECT * FROM pgr_aStar('SELECT id, source, target, cost, reverse_cost, x1, y1, x2, y2 FROM edges', "
     "6, 12)"},
    {"pgr_aStarCost",
     "Total cost of the A* shortest path for each requested start/end pair, without the path, sorted "
     "by start and end vertex.",
     "SELECT * FROM pgr_aStarCost('SELECT id, source, target, cost, reverse_cost, x1, y1, x2, y2 FROM "
     "edges', 6, 12)"},
    {"pgr_aStarCostMatrix",
     "A* shortest-path costs between every ordered pair of the given vertices, one row per pair.",
     "SELECT * FROM pgr_aStarCostMatrix('SELECT id, source, target, cost, reverse_cost, x1, y1, x2, y2 "
     "FROM edges', [5, 6, 10, 15])"},
    {"pgr_bdAstar",
     "Shortest paths by a bidirectional A* search, which grows from both ends toward each other, "
     "steered by each edge's end-point coordinates (x1, y1, x2, y2).",
     "SELECT * FROM pgr_bdAstar('SELECT id, source, target, cost, reverse_cost, x1, y1, x2, y2 FROM "
     "edges', 6, 12)"},
    {"pgr_bdAstarCost",
     "Total cost of the bidirectional A* shortest path for each requested start/end pair.",
     "SELECT * FROM pgr_bdAstarCost('SELECT id, source, target, cost, reverse_cost, x1, y1, x2, y2 FROM "
     "edges', 6, 12)"},
    {"pgr_bdAstarCostMatrix",
     "Bidirectional A* costs between every ordered pair of the given vertices, one row per pair.",
     "SELECT * FROM pgr_bdAstarCostMatrix('SELECT id, source, target, cost, reverse_cost, x1, y1, x2, "
     "y2 FROM edges', [5, 6, 10, 15])"},
    {"pgr_connectedComponents",
     "Groups the vertices of an undirected graph into connected components, one row per vertex; each "
     "component is numbered by its smallest vertex id.",
     "SELECT * FROM pgr_connectedComponents('SELECT id, source, target, cost, reverse_cost FROM edges')"},
    {"pgr_drivingDistance",
     "Every vertex reachable from one or more root vertices within a cost limit, with the "
     "shortest-path tree that reaches it: predecessor, depth and cost from its root.",
     "SELECT * FROM pgr_drivingDistance('SELECT id, source, target, cost, reverse_cost FROM edges', 11, "
     "3.0)"},
    {"pgr_withPointsDD",
     "Driving distance on the graph plus temporary points placed along its edges: every vertex and "
     "point within a cost limit of the roots; a point is addressed by the negative of its id.",
     "SELECT * FROM pgr_withPointsDD('SELECT id, source, target, cost, reverse_cost FROM edges', "
     "'SELECT pid, edge_id, fraction, side FROM pointsofinterest', -1, 3.3, 'r')"},
    {"pgr_kruskal",
     "The edges of a minimum spanning forest by Kruskal's algorithm: the cheapest edge set that "
     "connects every vertex of each connected component.",
     "SELECT * FROM pgr_kruskal('SELECT id, source, target, cost, reverse_cost FROM edges')"},
    {"pgr_kruskalBFS",
     "Walks Kruskal's minimum spanning forest breadth-first from the given roots, optionally only to a "
     "maximum depth.",
     "SELECT * FROM pgr_kruskalBFS('SELECT id, source, target, cost, reverse_cost FROM edges', 6)"},
    {"pgr_kruskalDFS",
     "Walks Kruskal's minimum spanning forest depth-first from the given roots, optionally only to a "
     "maximum depth.",
     "SELECT * FROM pgr_kruskalDFS('SELECT id, source, target, cost, reverse_cost FROM edges', 6)"},
    {"pgr_kruskalDD",
     "The part of Kruskal's minimum spanning forest within a cost limit of the given roots, measured "
     "along the tree.",
     "SELECT * FROM pgr_kruskalDD('SELECT id, source, target, cost, reverse_cost FROM edges', 6, 3.5)"},
    {"pgr_prim",
     "The edges of a minimum spanning forest grown by Prim's algorithm, one tree per connected "
     "component.",
     "SELECT * FROM pgr_prim('SELECT id, source, target, cost, reverse_cost FROM edges')"},
    {"pgr_primBFS",
     "Walks Prim's minimum spanning forest breadth-first from the given roots, optionally only to a "
     "maximum depth.",
     "SELECT * FROM pgr_primBFS('SELECT id, source, target, cost, reverse_cost FROM edges', 6)"},
    {"pgr_primDFS",
     "Walks Prim's minimum spanning forest depth-first from the given roots, optionally only to a "
     "maximum depth.",
     "SELECT * FROM pgr_primDFS('SELECT id, source, target, cost, reverse_cost FROM edges', 6)"},
    {"pgr_primDD",
     "The part of Prim's minimum spanning forest within a cost limit of the given roots, measured "
     "along the tree.",
     "SELECT * FROM pgr_primDD('SELECT id, source, target, cost, reverse_cost FROM edges', 6, 3.5)"},
    {"pgr_ksp",
     "Up to K loopless shortest paths between each start and end vertex, by Yen's algorithm, "
     "cheapest first; path_id numbers the paths across the whole answer.",
     "SELECT * FROM pgr_ksp('SELECT id, source, target, cost, reverse_cost FROM edges', 6, 17, 2)"},
    {"pgr_withPointsKSP",
     "K shortest loopless paths on the graph plus temporary points placed along its edges; a point "
     "is addressed by the negative of its id.",
     "SELECT * FROM pgr_withPointsKSP('SELECT id, source, target, cost, reverse_cost FROM edges', "
     "'SELECT pid, edge_id, fraction, side FROM pointsofinterest', -1, -2, 2, 'l')"},
    {"pgr_breadthFirstSearch",
     "Visits the graph breadth-first from the given roots, one row per vertex reached with its "
     "predecessor and depth, optionally only to a maximum depth.",
     "SELECT * FROM pgr_breadthFirstSearch('SELECT id, source, target, cost, reverse_cost FROM edges', 6)"},
    {"pgr_depthFirstSearch",
     "Visits the graph depth-first from the given roots, one row per vertex reached with its "
     "predecessor and depth, optionally only to a maximum depth.",
     "SELECT * FROM pgr_depthFirstSearch('SELECT id, source, target, cost, reverse_cost FROM edges', 6)"},
    {"pgr_extractVertices",
     "Lists the vertices of a graph found from its edges, either from their source and target ids or "
     "from the end points of their geometry (the latter needs the spatial extension), with the edges "
     "that enter and leave each vertex.",
     "SELECT * FROM pgr_extractVertices('SELECT id, source, target FROM edges')"},
    {"pgr_findCloseEdges",
     "For each given point, the nearest edges within a distance, with where along each edge the "
     "closest point lies, on which side of the edge the point is, and the connecting segment; needs "
     "the spatial extension.",
     "SELECT edge_id, fraction, side, distance FROM pgr_findCloseEdges('SELECT id, geom FROM edges', "
     "ST_Point(2.9, 1.8), 0.5, cap := 2)"},
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
