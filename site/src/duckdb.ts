// SPDX-License-Identifier: MIT
// One DuckDB-Wasm database per page. It is opened with unsigned extensions allowed — a setting
// DuckDB accepts only at start-up — and loads this site's own build of pgrouting, which is
// unsigned because only DuckDB's core and community repositories are signed.
import * as duckdb from '@duckdb/duckdb-wasm';
import ehWorker from '@duckdb/duckdb-wasm/dist/duckdb-browser-eh.worker.js?url';
import mvpWorker from '@duckdb/duckdb-wasm/dist/duckdb-browser-mvp.worker.js?url';
import ehModule from '@duckdb/duckdb-wasm/dist/duckdb-eh.wasm?url';
import mvpModule from '@duckdb/duckdb-wasm/dist/duckdb-mvp.wasm?url';
import { extensionPath } from './paths.ts';
import { plainValue, type ResultSet } from './result.ts';

// apache-arrow's Type.Decimal; the enum is not imported so that arrow stays an indirect dependency.
const ARROW_DECIMAL = 7;

const BUNDLES: duckdb.DuckDBBundles = {
  mvp: { mainModule: mvpModule, mainWorker: mvpWorker },
  eh: { mainModule: ehModule, mainWorker: ehWorker },
};

export interface Session {
  duckdbVersion: string;
  variant: string;
  pgrVersion: string;
  query(sql: string): Promise<ResultSet>;
}

export class ExtensionLoadError extends Error {
  duckdbVersion: string;
  variant: string;
  url: string;
  constructor(duckdbVersion: string, variant: string, url: string, cause: unknown) {
    super(`No usable pgrouting build for DuckDB ${duckdbVersion} (${variant}) at ${url}: ${String(cause)}`);
    this.duckdbVersion = duckdbVersion;
    this.variant = variant;
    this.url = url;
  }
}

const SAMPLE_TABLES: [name: string, select: string][] = [
  ['edges', "SELECT * FROM read_csv('edges.csv')"],
  [
    'vertices',
    "SELECT id, CAST(in_edges AS BIGINT[]) AS in_edges, CAST(out_edges AS BIGINT[]) AS out_edges, x, y FROM read_csv('vertices.csv')",
  ],
  ['pointsofinterest', "SELECT * FROM read_csv('pointsofinterest.csv')"],
  ['combinations', "SELECT * FROM read_csv('combinations.csv')"],
  [
    'restrictions',
    "SELECT id, CAST(path AS BIGINT[]) AS path, CAST(cost AS DOUBLE) AS cost FROM read_csv('restrictions.csv')",
  ],
];

function sqlString(text: string): string {
  return `'${text.replaceAll("'", "''")}'`;
}

function toResultSet(table: Awaited<ReturnType<duckdb.AsyncDuckDBConnection['query']>>): ResultSet {
  const fields = table.schema.fields;
  const columns = fields.map((f) => f.name);
  // DECIMAL values arrive unscaled; the field type carries the scale.
  const scales = fields.map((f) =>
    f.typeId === ARROW_DECIMAL ? (f.type as unknown as { scale: number }).scale : undefined,
  );
  const vectors = fields.map((_, i) => table.getChildAt(i));
  const rows = [];
  for (let r = 0; r < table.numRows; r++) rows.push(vectors.map((v, i) => plainValue(v?.get(r), scales[i])));
  return { columns, rows };
}

export async function startSession(): Promise<Session> {
  const bundle = await duckdb.selectBundle(BUNDLES);
  const variant = bundle.mainModule === ehModule ? 'wasm_eh' : 'wasm_mvp';
  if (!bundle.mainWorker) throw new Error('DuckDB-Wasm bundle has no worker');
  const db = new duckdb.AsyncDuckDB(new duckdb.VoidLogger(), new Worker(bundle.mainWorker));
  await db.instantiate(bundle.mainModule, bundle.pthreadWorker);
  await db.open({ allowUnsignedExtensions: true });
  const conn = await db.connect();
  const query = async (sql: string) => toResultSet(await conn.query(sql));

  const duckdbVersion = String((await query('SELECT version()')).rows[0]?.[0]);
  const url =
    (import.meta.env.DEV && import.meta.env.VITE_PGROUTING_EXTENSION_URL) ||
    new URL(`${import.meta.env.BASE_URL}${extensionPath(duckdbVersion, variant)}`, location.origin).href;
  try {
    await conn.query(`LOAD ${sqlString(url)}`);
  } catch (error) {
    throw new ExtensionLoadError(duckdbVersion, variant, url, error instanceof Error ? error.message : error);
  }

  for (const [name] of SAMPLE_TABLES) {
    const response = await fetch(`${import.meta.env.BASE_URL}data/${name}.csv`);
    if (!response.ok) throw new Error(`Could not fetch data/${name}.csv: HTTP ${response.status}`);
    await db.registerFileText(`${name}.csv`, await response.text());
  }
  for (const [name, select] of SAMPLE_TABLES) await conn.query(`CREATE TABLE ${name} AS ${select}`);

  const pgrVersion = String((await query('SELECT pgr_version()')).rows[0]?.[0]);
  return { duckdbVersion, variant, pgrVersion, query };
}
