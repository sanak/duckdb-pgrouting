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
    {"pgr_dijkstraVia",
     "A route that visits the given vertices in order, as consecutive Dijkstra shortest paths; each "
     "row also carries the cost accumulated along the whole route.",
     "SELECT * FROM pgr_dijkstraVia('SELECT id, source, target, cost, reverse_cost FROM edges', "
     "[5, 7, 1, 8, 15])"},
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
    {"pgr_strongComponents",
     "Groups the vertices of a directed graph into strongly connected components, in which every "
     "vertex can reach every other; each component is numbered by its smallest vertex id.",
     "SELECT * FROM pgr_strongComponents('SELECT id, source, target, cost, reverse_cost FROM edges')"},
    {"pgr_biconnectedComponents",
     "Groups the edges of an undirected graph into biconnected components, which stay connected when "
     "any single vertex is removed; each component is numbered by its smallest edge id.",
     "SELECT * FROM pgr_biconnectedComponents('SELECT id, source, target, cost, reverse_cost FROM edges')"},
    {"pgr_articulationPoints",
     "The vertices of an undirected graph whose removal would split the part of the graph they are in.",
     "SELECT * FROM pgr_articulationPoints('SELECT id, source, target, cost, reverse_cost FROM edges')"},
    {"pgr_bridges",
     "The edges of an undirected graph whose removal would split the part of the graph they are in.",
     "SELECT * FROM pgr_bridges('SELECT id, source, target, cost, reverse_cost FROM edges')"},
    {"pgr_makeConnected",
     "The fewest new edges, as vertex pairs, that would join all the connected parts of an undirected "
     "graph into one; which vertices they join depends on the order the edges are read in.",
     "SELECT * FROM pgr_makeConnected('SELECT id, source, target, cost, reverse_cost FROM edges')"},
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
     "Up to K cheapest loopless paths between each start and end vertex, by Yen's algorithm (more "
     "with heap_paths); path_id numbers the paths across the whole answer.",
     "SELECT * FROM pgr_ksp('SELECT id, source, target, cost, reverse_cost FROM edges', 6, 17, 2)"},
    {"pgr_withPointsKSP",
     "Up to K cheapest loopless paths (more with heap_paths) on the graph plus temporary points "
     "placed along its edges; a point is addressed by the negative of its id.",
     "SELECT * FROM pgr_withPointsKSP('SELECT id, source, target, cost, reverse_cost FROM edges', "
     "'SELECT pid, edge_id, fraction, side FROM pointsofinterest', -1, -2, 2, 'l')"},
    {"pgr_turnRestrictedPath",
     "A path between two vertices that breaks none of the given turn restrictions, searched among up "
     "to K candidate paths by Yen's algorithm; strict returns nothing when every candidate breaks one.",
     "SELECT * FROM pgr_turnRestrictedPath('SELECT id, source, target, cost, reverse_cost FROM edges', "
     "'SELECT path, cost FROM restrictions', 3, 8, 3)"},
    {"pgr_withPointsVia",
     "A route through the given vertices and temporary points, in order, as consecutive shortest "
     "paths on the graph plus points placed along its edges.",
     "SELECT * FROM pgr_withPointsVia('SELECT id, source, target, cost, reverse_cost FROM edges', "
     "'SELECT pid, edge_id, fraction, side FROM pointsofinterest', [-1, 7, -3, 16, 15])"},
    {"pgr_trsp",
     "Shortest paths that honour turn restrictions: each restriction row names a sequence of edges "
     "(path) whose traversal costs extra (cost).",
     "SELECT * FROM pgr_trsp('SELECT id, source, target, cost, reverse_cost FROM edges', "
     "'SELECT path, cost FROM restrictions', 1, 8)"},
    {"pgr_trsp_withPoints",
     "Turn-restricted shortest paths on the graph plus temporary points placed along its edges; a "
     "point is addressed by the negative of its id.",
     "SELECT * FROM pgr_trsp_withPoints('SELECT id, source, target, cost, reverse_cost FROM edges', "
     "'SELECT path, cost FROM restrictions', 'SELECT pid, edge_id, fraction, side FROM pointsofinterest', "
     "-1, 10)"},
    {"pgr_trspVia",
     "A route that visits the given vertices in order, as consecutive turn-restricted shortest paths; "
     "each row also carries the cost along the whole route.",
     "SELECT * FROM pgr_trspVia('SELECT id, source, target, cost, reverse_cost FROM edges', "
     "'SELECT path, cost FROM restrictions', [5, 7, 1, 8, 15])"},
    {"pgr_trspVia_withPoints",
     "A turn-restricted route through the given vertices and temporary points, in order, on the graph "
     "plus points placed along its edges.",
     "SELECT * FROM pgr_trspVia_withPoints('SELECT id, source, target, cost, reverse_cost FROM edges', "
     "'SELECT path, cost FROM restrictions', 'SELECT pid, edge_id, side, fraction FROM pointsofinterest', "
     "[-6, 15, -5])"},
    {"pgr_TSP",
     "An approximate shortest round trip through every vertex of a cost matrix (start_vid, end_vid, "
     "agg_cost), optionally from start_id and visiting end_id last; the tour found can depend on the "
     "order the matrix rows are read in.",
     "SELECT * FROM pgr_TSP('SELECT * FROM pgr_dijkstraCostMatrix(''SELECT id, source, target, cost, "
     "reverse_cost FROM edges'', [5, 6, 10, 15], directed := false)', start_id := 5)"},
    {"pgr_TSPeuclidean",
     "An approximate shortest round trip through points given by their coordinates (id, x, y), with "
     "straight-line distances as costs, optionally from start_id and visiting end_id last.",
     "SELECT * FROM pgr_TSPeuclidean('SELECT id, x, y FROM vertices', start_id := 1)"},
    {"pgr_breadthFirstSearch",
     "Visits the graph breadth-first from the given roots, one row per vertex reached with its "
     "predecessor and depth, optionally only to a maximum depth.",
     "SELECT * FROM pgr_breadthFirstSearch('SELECT id, source, target, cost, reverse_cost FROM edges', 6)"},
    {"pgr_depthFirstSearch",
     "Visits the graph depth-first from the given roots, one row per vertex reached with its "
     "predecessor and depth, optionally only to a maximum depth.",
     "SELECT * FROM pgr_depthFirstSearch('SELECT id, source, target, cost, reverse_cost FROM edges', 6)"},
    {"pgr_sequentialVertexColoring",
     "Colors the vertices one by one so that no edge joins two vertices of the same color, colors "
     "numbered from 1; the coloring found depends on the order the edges are read in.",
     "SELECT * FROM pgr_sequentialVertexColoring('SELECT id, source, target, cost, reverse_cost FROM edges')"},
    {"pgr_bipartite",
     "Splits the vertices into two sides, color 0 and color 1, so that every edge joins the two sides; "
     "returns no rows when the graph cannot be split that way.",
     "SELECT * FROM pgr_bipartite('SELECT id, source, target, cost, reverse_cost FROM edges')"},
    {"pgr_edgeColoring",
     "Colors the edges of an undirected graph so that no two edges meeting at a vertex share a color, "
     "colors numbered from 1; the coloring found depends on the order the edges are read in.",
     "SELECT * FROM pgr_edgeColoring('SELECT id, source, target, cost, reverse_cost FROM edges')"},
    {"pgr_cuthillMckeeOrdering",
     "Renumbers the vertices in reverse Cuthill-McKee order, which keeps the two ends of every edge "
     "close in the numbering (a small bandwidth); how ties between vertices of equal degree break "
     "depends on the edge order and the platform.",
     "SELECT * FROM pgr_cuthillMckeeOrdering('SELECT id, source, target, cost, reverse_cost FROM edges')"},
    {"pgr_kingOrdering",
     "Renumbers the vertices in King's order, which keeps the numbering's profile small; how ties "
     "between vertices of equal degree break depends on the edge order and the platform.",
     "SELECT * FROM pgr_kingOrdering('SELECT id, source, target, cost, reverse_cost FROM edges')"},
    {"pgr_sloanOrdering",
     "Renumbers the vertices in Sloan's order, which keeps the numbering's profile and wavefront small; "
     "only one connected part is ordered, the remaining positions repeating the smallest vertex id.",
     "SELECT * FROM pgr_sloanOrdering('SELECT id, source, target, cost, reverse_cost FROM edges')"},
    {"pgr_topologicalSort",
     "Orders the vertices of a directed graph without cycles so that every edge points forward in the "
     "order; a graph with a cycle is an error.",
     "SELECT * FROM pgr_topologicalSort('SELECT id, source, target, cost FROM edges WHERE cost >= 0')"},
    {"pgr_johnson",
     "Shortest-path costs between every ordered pair of vertices that are connected, by Johnson's "
     "algorithm, suited to sparse graphs; memory grows with the square of the number of vertices.",
     "SELECT * FROM pgr_johnson('SELECT id, source, target, cost, reverse_cost FROM edges WHERE id < 5')"},
    {"pgr_floydWarshall",
     "Shortest-path costs between every ordered pair of vertices that are connected, by the "
     "Floyd-Warshall algorithm, suited to dense graphs; memory grows with the square of the number of "
     "vertices and time with its cube.",
     "SELECT * FROM pgr_floydWarshall('SELECT id, source, target, cost, reverse_cost FROM edges WHERE id < 5')"},
    {"pgr_betweennessCentrality",
     "For each vertex, the share of shortest paths between other vertices that pass through it "
     "(Brandes' algorithm, normalized by the number of vertex pairs).",
     "SELECT * FROM pgr_betweennessCentrality('SELECT id, source, target, cost, reverse_cost FROM edges WHERE id < 5')"},
    {"pgr_bandwidth",
     "The bandwidth of an undirected graph: the largest gap between the positions of two joined "
     "vertices when the vertices are numbered in ascending id order; one value, also callable as a "
     "scalar function.",
     "SELECT pgr_bandwidth('SELECT id, source, target, cost, reverse_cost FROM edges')"},
    {"pgr_isPlanar",
     "Whether an undirected graph can be drawn in the plane without crossing edges (the "
     "Boyer-Myrvold test); false for an empty edge set; one value, also callable as a scalar function.",
     "SELECT pgr_isPlanar('SELECT id, source, target, cost, reverse_cost FROM edges')"},
    {"pgr_lineGraph",
     "The line graph of a graph: one vertex per edge, joined where one edge can follow another; "
     "reverse_cost marks a pair that joins both ways. Which way a two-way pair is written depends on "
     "the order the edges are read in.",
     "SELECT * FROM pgr_lineGraph('SELECT id, source, target, cost, reverse_cost FROM edges WHERE id IN "
     "(2, 4, 5, 8)', false)"},
    {"pgr_lineGraphFull",
     "The full line graph of a directed graph: each vertex becomes one vertex per incident edge end, "
     "joined by zero-cost turns, so turn costs and restrictions can be added before routing on it; "
     "the new vertices get negative ids.",
     "SELECT * FROM pgr_lineGraphFull('SELECT id, source, target, cost, reverse_cost FROM edges WHERE "
     "id IN (4, 7, 8, 10)')"},
    {"pgr_transitiveClosure",
     "For each vertex of a directed graph, the list of vertices it can reach; the order inside each "
     "list depends on the order the edges are read in, and memory grows with the square of the number "
     "of vertices.",
     "SELECT * FROM pgr_transitiveClosure('SELECT id, source, target, cost, reverse_cost FROM edges "
     "WHERE id IN (2, 3, 5, 11, 12, 13, 15)')"},
    {"pgr_lengauerTarjanDominatorTree",
     "The immediate dominator of every vertex of a directed graph, seen from a root vertex: the last "
     "vertex every path from the root must pass through. idom is the seq of the dominator's own row "
     "(0 for the root and unreachable vertices), not a vertex id.",
     "SELECT * FROM pgr_lengauerTarjanDominatorTree('SELECT id, source, target, cost, reverse_cost FROM "
     "edges', 5)"},
    {"pgr_stoerWagner",
     "A minimum cut of a connected undirected graph: the cheapest set of edges whose removal splits it "
     "in two, with the running total of their costs; among equally cheap cuts, which one depends on "
     "the order the edges are read in.",
     "SELECT * FROM pgr_stoerWagner('SELECT id, source, target, cost, reverse_cost FROM edges WHERE id "
     "< 17')"},
    {"pgr_hawickCircuits",
     "Every elementary circuit of a directed graph, self-loops included, one row per step with a "
     "closing row back at the start; where each circuit starts, and so the numbering, depends on the "
     "order the edges are read in, and the number of circuits can grow very fast with the graph.",
     "SELECT * FROM pgr_hawickCircuits('SELECT id, source, target, cost, reverse_cost FROM edges WHERE "
     "id < 5')"},
    {"pgr_maxFlow",
     "The value of a maximum flow from one or many sources to one or many sinks (push-relabel); one "
     "value, also callable as a scalar function.",
     "SELECT pgr_maxFlow('SELECT id, source, target, capacity, reverse_capacity FROM edges', 11, 12)"},
    {"pgr_pushRelabel",
     "A maximum flow from one or many sources to one or many sinks by the push-relabel algorithm, one "
     "row per edge direction that carries flow, with the capacity left on it; which edges carry it "
     "can depend on the order the edges are read in, the total cannot.",
     "SELECT * FROM pgr_pushRelabel('SELECT id, source, target, capacity, reverse_capacity FROM edges', 11, 12)"},
    {"pgr_boykovKolmogorov",
     "A maximum flow from one or many sources to one or many sinks by the Boykov-Kolmogorov algorithm, "
     "one row per edge direction that carries flow, with the capacity left on it.",
     "SELECT * FROM pgr_boykovKolmogorov('SELECT id, source, target, capacity, reverse_capacity FROM edges', "
     "11, 12)"},
    {"pgr_edmondsKarp",
     "A maximum flow from one or many sources to one or many sinks by the Edmonds-Karp algorithm, one row "
     "per edge direction that carries flow, with the capacity left on it.",
     "SELECT * FROM pgr_edmondsKarp('SELECT id, source, target, capacity, reverse_capacity FROM edges', 11, 12)"},
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
    {"pgr_degree",
     "The number of edge ends at each vertex, counted from the edges' source and target (a self-loop "
     "counts twice), or from a vertices query's in_edges and out_edges lists restricted to the given "
     "edges; sorted by vertex.",
     "SELECT * FROM pgr_degree('SELECT id, source, target FROM edges')"},
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
