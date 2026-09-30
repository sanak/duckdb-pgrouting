// SPDX-License-Identifier: GPL-2.0-or-later
// Browser side of W2: starts the pinned DuckDB-Wasm from this server, loads pgrouting, and runs
// the smoke queries and one query per function family on the sample graph, the geometry ones
// with duckdb-spatial loaded. Bundled by esbuild because the package's ESM build imports
// apache-arrow by bare specifier, which a browser cannot resolve on its own.

import * as duckdb from '@duckdb/duckdb-wasm';

const BUNDLES = {
  mvp: { mainModule: '/duckdb/duckdb-mvp.wasm', mainWorker: '/duckdb/duckdb-browser-mvp.worker.js' },
  eh: { mainModule: '/duckdb/duckdb-eh.wasm', mainWorker: '/duckdb/duckdb-browser-eh.worker.js' },
};

const EDGES = 'SELECT id, source, target, cost, reverse_cost FROM edges';
const EDGES_XY = 'SELECT id, source, target, cost, reverse_cost, x1, y1, x2, y2 FROM edges';
const FLOW_EDGES = 'SELECT id, source, target, capacity, reverse_capacity FROM edges';

// Arrow returns BIGINT as BigInt, which does not survive the trip back to the test.
function plain(row) {
  return Object.fromEntries(
    Object.entries(row.toJSON()).map(([key, value]) => [key, typeof value === 'bigint' ? Number(value) : value]),
  );
}

async function rows(conn, sql) {
  return (await conn.query(sql)).toArray().map(plain);
}

window.runW2 = async function runW2(source) {
  const out = {};
  try {
    const bundle = await duckdb.selectBundle(BUNDLES);
    out.variant = bundle.mainModule === BUNDLES.eh.mainModule ? 'wasm_eh' : 'wasm_mvp';
    const db = new duckdb.AsyncDuckDB(new duckdb.VoidLogger(), new Worker(bundle.mainWorker));
    await db.instantiate(bundle.mainModule, bundle.pthreadWorker);
    await db.open({ allowUnsignedExtensions: true });
    const conn = await db.connect();
    out.duckdbVersion = (await rows(conn, 'SELECT version() AS v'))[0].v;

    if (source.community) {
      await conn.query('INSTALL pgrouting FROM community');
      await conn.query('LOAD pgrouting');
    } else {
      const path = source.urlTemplate.replaceAll('{version}', out.duckdbVersion).replaceAll('{variant}', out.variant);
      out.extensionUrl = new URL(path, location.href).href;
      await conn.query(`LOAD '${out.extensionUrl}'`);
    }
    out.extensionVersion = (
      await rows(conn, "SELECT extension_version AS v FROM duckdb_extensions() WHERE extension_name = 'pgrouting'")
    )[0]?.v;
    out.pgrVersion = (await rows(conn, 'SELECT pgr_version() AS v'))[0].v;

    await db.registerFileText('edges.csv', await (await fetch('/data/edges.csv')).text());
    // geom is WKT in the CSV; GEOMETRY is a core type, so the cast needs no spatial.
    await conn.query(
      "CREATE TABLE edges AS SELECT * REPLACE (CAST(geom AS GEOMETRY) AS geom) FROM read_csv_auto('edges.csv')",
    );
    await db.registerFileText('restrictions.csv', await (await fetch('/data/restrictions.csv')).text());
    // path is a list written as text in the CSV, e.g. "[4, 7]".
    await conn.query(
      "CREATE TABLE restrictions AS SELECT CAST(path AS BIGINT[]) AS path, CAST(cost AS DOUBLE) AS cost FROM read_csv_auto('restrictions.csv')",
    );
    out.dijkstra = await rows(
      conn,
      `SELECT path_seq, node, edge, agg_cost FROM pgr_dijkstra('${EDGES}', 6, 10) ORDER BY path_seq`,
    );
    out.dijkstraCost = await rows(conn, `SELECT start_vid, end_vid, agg_cost FROM pgr_dijkstraCost('${EDGES}', 6, 10)`);
    out.components = await rows(
      conn,
      `SELECT count(*) AS n, count(DISTINCT component) AS components FROM pgr_connectedComponents('${EDGES}')`,
    );
    // One query per function family. They run before spatial is loaded, so a failing family is
    // reported as its own error rather than never reached after a spatial download failure.
    out.aStar = await rows(
      conn,
      `SELECT path_seq, node, edge, agg_cost FROM pgr_aStar('${EDGES_XY}', 6, 12) ORDER BY path_seq`,
    );
    out.drivingDistance = await rows(
      conn,
      `SELECT node, agg_cost FROM pgr_drivingDistance('${EDGES}', 11, 3.0) ORDER BY node`,
    );
    out.kruskal = await rows(conn, `SELECT count(*) AS n, sum(cost) AS total FROM pgr_kruskal('${EDGES}')`);
    out.bfs = await rows(conn, `SELECT node, depth FROM pgr_breadthFirstSearch('${EDGES}', 6) ORDER BY node`);
    out.dfs = await rows(conn, `SELECT node FROM pgr_depthFirstSearch('${EDGES}', 6) ORDER BY node`);
    out.ksp = await rows(
      conn,
      `SELECT path_id, max(agg_cost) AS cost FROM pgr_ksp('${EDGES}', 6, 17, 2) GROUP BY path_id ORDER BY path_id`,
    );
    out.via = await rows(
      conn,
      `SELECT count(DISTINCT path_id) AS legs, max(route_agg_cost) AS total FROM pgr_dijkstraVia('${EDGES}', [5, 7, 1, 8, 15])`,
    );
    out.trsp = await rows(
      conn,
      `SELECT path_seq, node, edge, agg_cost FROM pgr_trsp('${EDGES}', 'SELECT path, cost FROM restrictions', 1, 8) ORDER BY path_seq`,
    );
    out.tsp = await rows(
      conn,
      `SELECT seq, node FROM pgr_TSP('SELECT * FROM pgr_dijkstraCostMatrix(''${EDGES}'', [5, 6, 10, 15], directed := false)', start_id := 5) ORDER BY seq`,
    );
    // Families added since v0.3.0. Lists and integer sums are folded into VARCHAR or BIGINT in
    // SQL, so every value crosses Arrow as a plain scalar.
    out.articulationPoints = await rows(
      conn,
      `SELECT string_agg(CAST(node AS VARCHAR), ',' ORDER BY node) AS nodes FROM pgr_articulationPoints('${EDGES}')`,
    );
    out.coloring = await rows(
      conn,
      `WITH c AS MATERIALIZED (SELECT * FROM pgr_sequentialVertexColoring('${EDGES}'))
       SELECT (SELECT count(*) FROM c) AS n, count(*) FILTER (WHERE a.color = b.color) AS clashes
       FROM edges e JOIN c a ON a.node = e.source JOIN c b ON b.node = e.target`,
    );
    out.cuthillMckee = await rows(
      conn,
      `SELECT count(*) AS n, count(DISTINCT node) AS nodes, min(seq) AS first, max(seq) AS last FROM pgr_cuthillMckeeOrdering('${EDGES}')`,
    );
    out.floydWarshall = await rows(
      conn,
      `SELECT count(*) AS n, sum(agg_cost) AS total FROM pgr_floydWarshall('${EDGES}')`,
    );
    out.betweenness = await rows(
      conn,
      `SELECT count(*) AS n, arg_max(vid, centrality) AS top, max(centrality) AS centrality FROM pgr_betweennessCentrality('${EDGES}')`,
    );
    out.isPlanar = await rows(conn, `SELECT pgr_isPlanar('${EDGES}') AS planar`);
    out.lineGraph = await rows(conn, `SELECT count(*) AS n FROM pgr_lineGraph('${EDGES}')`);
    out.transitiveClosure = await rows(
      conn,
      `SELECT string_agg(node || ':' || CAST(list_sort(targets) AS VARCHAR), ' ' ORDER BY node) AS closure
       FROM pgr_transitiveClosure('${EDGES} WHERE id IN (2, 3, 5, 11, 12, 13, 15)')`,
    );
    out.dominator = await rows(
      conn,
      `WITH t AS MATERIALIZED (SELECT * FROM pgr_lengauerTarjanDominatorTree('${EDGES}', 5))
       SELECT string_agg(t.vertex_id || '>' || coalesce(d.vertex_id, 0), ',' ORDER BY t.vertex_id) AS pairs
       FROM t LEFT JOIN t d ON t.idom = d.seq`,
    );
    out.hawick = await rows(
      conn,
      `SELECT count(*) AS n, count(DISTINCT path_id) AS circuits, sum(cost) AS total FROM pgr_hawickCircuits('${EDGES}')`,
    );
    out.stoerWagner = await rows(
      conn,
      `SELECT max(mincut) AS mincut FROM pgr_stoerWagner('${EDGES} WHERE id < 17')`,
    );
    out.maxFlow = await rows(conn, `SELECT pgr_maxFlow('${FLOW_EDGES}', 11, 12) AS flow`);
    out.pushRelabel = await rows(
      conn,
      `SELECT CAST(sum(flow) FILTER (WHERE start_vid = 11) - coalesce(sum(flow) FILTER (WHERE end_vid = 11), 0) AS BIGINT) AS net
       FROM pgr_pushRelabel('${FLOW_EDGES}', 11, 12)`,
    );
    out.chinesePostmanCost = await rows(conn, `SELECT pgr_chinesePostmanCost('${EDGES} WHERE id < 17') AS cost`);
    out.contraction = await rows(
      conn,
      `SELECT type, count(*) AS n, CAST(sum(len(contracted_vertices)) AS BIGINT) AS absorbed
       FROM pgr_contraction('${EDGES}', false) GROUP BY type ORDER BY type`,
    );
    out.contractionHierarchies = await rows(
      conn,
      `SELECT count(*) AS ranked FROM pgr_contractionHierarchies('${EDGES}', false) WHERE type = 'v'`,
    );
    out.degree = await rows(
      conn,
      `SELECT string_agg(node || ':' || degree, ',' ORDER BY node) AS degrees FROM pgr_degree('SELECT id, source, target FROM edges')`,
    );
    // DuckDB-Wasm installs spatial from extensions.duckdb.org on LOAD.
    await conn.query('LOAD spatial');
    out.vertices = await rows(conn, "SELECT count(*) AS n FROM pgr_extractVertices('SELECT id, geom FROM edges')");
    out.closeEdges = await rows(
      conn,
      "SELECT edge_id, fraction, side FROM pgr_findCloseEdges('SELECT id, geom FROM edges', 'POINT(2.9 1.8)'::GEOMETRY, 0.5)",
    );
  } catch (error) {
    out.error = String(error?.message ?? error);
  }
  return out;
};
