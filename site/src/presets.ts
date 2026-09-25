// SPDX-License-Identifier: MIT
// Starting points for the editor, one or more per function. Every query runs on the sample tables
// the Playground creates (edges, vertices, pointsofinterest, combinations, restrictions).

export interface Preset {
  id: string;
  label: string;
  sql: string;
}

const EDGES = "'SELECT id, source, target, cost, reverse_cost FROM edges'";
const POINTS = "'SELECT pid, edge_id, fraction, side FROM pointsofinterest'";

export const PRESETS: readonly Preset[] = [
  {
    id: 'dijkstra-one-to-one',
    label: 'pgr_dijkstra — one to one',
    sql: `-- Shortest path from vertex 5 to vertex 12 on the directed sample graph.
SELECT * FROM pgr_dijkstra(${EDGES}, 5, 12);`,
  },
  {
    id: 'dijkstra-many-to-many',
    label: 'pgr_dijkstra — many to many, undirected',
    sql: `-- Every start in the first list to every end in the second, ignoring edge direction.
SELECT * FROM pgr_dijkstra(${EDGES}, [5, 3], [12, 17], directed => false);`,
  },
  {
    id: 'dijkstra-combinations',
    label: 'pgr_dijkstra — pairs from a table',
    sql: `-- Only the (source, target) pairs listed in the combinations table.
SELECT * FROM pgr_dijkstra(${EDGES}, 'SELECT source, target FROM combinations');`,
  },
  {
    id: 'dijkstra-cost',
    label: 'pgr_dijkstraCost',
    sql: `-- Just the total cost of each route, without the path.
SELECT * FROM pgr_dijkstraCost(${EDGES}, [5, 3], [12, 17]);`,
  },
  {
    id: 'dijkstra-cost-matrix',
    label: 'pgr_dijkstraCostMatrix',
    sql: `-- Costs between every ordered pair of the listed vertices.
SELECT * FROM pgr_dijkstraCostMatrix(${EDGES}, [5, 7, 12, 16]);`,
  },
  {
    id: 'dijkstra-near',
    label: 'pgr_dijkstraNear',
    sql: `-- The route from vertex 5 to whichever of 12, 15 and 17 is cheapest to reach.
SELECT * FROM pgr_dijkstraNear(${EDGES}, 5, [12, 15, 17]);`,
  },
  {
    id: 'dijkstra-near-cost',
    label: 'pgr_dijkstraNearCost',
    sql: `-- The same nearest target, cost only.
SELECT * FROM pgr_dijkstraNearCost(${EDGES}, 5, [12, 15, 17]);`,
  },
  {
    id: 'with-points',
    label: 'pgr_withPoints',
    sql: `-- From point of interest 1 to point 3; negative ids are points, details shows points passed.
SELECT * FROM pgr_withPoints(${EDGES}, ${POINTS}, -1, -3, details => true);`,
  },
  {
    id: 'with-points-cost',
    label: 'pgr_withPointsCost',
    sql: `-- Costs between points and vertices mixed together.
SELECT * FROM pgr_withPointsCost(${EDGES}, ${POINTS}, [-1, 5], [-3, 12]);`,
  },
  {
    id: 'with-points-cost-matrix',
    label: 'pgr_withPointsCostMatrix',
    sql: `-- A cost matrix over two points and one vertex.
SELECT * FROM pgr_withPointsCostMatrix(${EDGES}, ${POINTS}, [-1, -3, 5]);`,
  },
  {
    id: 'bd-dijkstra',
    label: 'pgr_bdDijkstra',
    sql: `-- Bidirectional search: from both ends at once.
SELECT * FROM pgr_bdDijkstra(${EDGES}, 5, 12);`,
  },
  {
    id: 'bd-dijkstra-cost',
    label: 'pgr_bdDijkstraCost',
    sql: `-- Bidirectional search, cost only.
SELECT * FROM pgr_bdDijkstraCost(${EDGES}, [5, 3], [12, 17]);`,
  },
  {
    id: 'bd-dijkstra-cost-matrix',
    label: 'pgr_bdDijkstraCostMatrix',
    sql: `-- Bidirectional search over every ordered pair.
SELECT * FROM pgr_bdDijkstraCostMatrix(${EDGES}, [5, 7, 12, 16]);`,
  },
  {
    id: 'bellman-ford',
    label: 'pgr_bellmanFord',
    sql: `-- Bellman-Ford also accepts negative costs (none in the sample data).
SELECT * FROM pgr_bellmanFord(${EDGES}, 5, [12, 17]);`,
  },
  {
    id: 'edward-moore',
    label: 'pgr_edwardMoore',
    sql: `-- Edward Moore: a queue-based variant of Bellman-Ford.
SELECT * FROM pgr_edwardMoore(${EDGES}, 5, [12, 17]);`,
  },
  {
    id: 'dag-shortest-path',
    label: 'pgr_dagShortestPath',
    sql: `-- For a directed acyclic graph: the forward costs only (no reverse_cost).
SELECT * FROM pgr_dagShortestPath('SELECT id, source, target, cost FROM edges', 5, [11, 12]);`,
  },
  {
    id: 'binary-bfs',
    label: 'pgr_binaryBreadthFirstSearch',
    sql: `-- Costs must be 0 or 1: here even edge ids are free and odd ones cost 1.
SELECT * FROM pgr_binaryBreadthFirstSearch(
  'SELECT id, source, target,
          CASE WHEN id % 2 = 0 THEN 0 ELSE 1 END AS cost,
          CASE WHEN id % 2 = 0 THEN 0 ELSE 1 END AS reverse_cost
   FROM edges',
  5, 12);`,
  },
  {
    id: 'version',
    label: 'pgr_version',
    sql: `-- The pgRouting version built into the extension.
SELECT pgr_version();`,
  },
];
