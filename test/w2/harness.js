// SPDX-License-Identifier: GPL-2.0-or-later
// Browser side of W2: starts the pinned DuckDB-Wasm from this server, loads pgrouting, and runs
// the smoke queries on the sample graph. Bundled by esbuild because the package's ESM build
// imports apache-arrow by bare specifier, which a browser cannot resolve on its own.

import * as duckdb from '@duckdb/duckdb-wasm';

const BUNDLES = {
  mvp: { mainModule: '/duckdb/duckdb-mvp.wasm', mainWorker: '/duckdb/duckdb-browser-mvp.worker.js' },
  eh: { mainModule: '/duckdb/duckdb-eh.wasm', mainWorker: '/duckdb/duckdb-browser-eh.worker.js' },
};

const EDGES = 'SELECT id, source, target, cost, reverse_cost FROM edges';

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
    await conn.query("CREATE TABLE edges AS SELECT * FROM read_csv_auto('edges.csv')");
    out.dijkstra = await rows(
      conn,
      `SELECT path_seq, node, edge, agg_cost FROM pgr_dijkstra('${EDGES}', 6, 10) ORDER BY path_seq`,
    );
    out.dijkstraCost = await rows(conn, `SELECT start_vid, end_vid, agg_cost FROM pgr_dijkstraCost('${EDGES}', 6, 10)`);
  } catch (error) {
    out.error = String(error?.message ?? error);
  }
  return out;
};
