// SPDX-License-Identifier: MIT
// One DuckDB-Wasm database per page. It is opened with unsigned extensions allowed — a setting
// DuckDB accepts only at start-up — and loads this site's own build of pgrouting, which is
// unsigned because only DuckDB's core and community repositories are signed. Datasets are built
// separately (loader.ts).
import * as duckdb from '@duckdb/duckdb-wasm';
import ehWorker from '@duckdb/duckdb-wasm/dist/duckdb-browser-eh.worker.js?url';
import mvpWorker from '@duckdb/duckdb-wasm/dist/duckdb-browser-mvp.worker.js?url';
import ehModule from '@duckdb/duckdb-wasm/dist/duckdb-eh.wasm?url';
import mvpModule from '@duckdb/duckdb-wasm/dist/duckdb-mvp.wasm?url';
import { extensionPath } from './paths.ts';
import { plainValue, type ResultSet, type Shape } from './result.ts';

// apache-arrow's Type ids; the enum is not imported so that arrow stays an indirect dependency.
const ARROW_DECIMAL = 7;
const ARROW_BINARY = 4;
const ARROW_LIST = 12;
const ARROW_FIXED_SIZE_LIST = 16;

// The parts of an Arrow DataType that shapeOf reads.
interface ArrowType {
  typeId: number;
  scale?: number;
  children?: { type: ArrowType; metadata?: Map<string, string> }[];
}

// DECIMAL values arrive unscaled, also as list elements; the type carries the scale. A binary
// field is a geometry when its extension name says geoarrow.wkb.
function shapeOf(type: ArrowType, metadata?: Map<string, string>): Shape | undefined {
  if (type.typeId === ARROW_DECIMAL) return { scale: type.scale ?? 0 };
  if (type.typeId === ARROW_BINARY) {
    return { binary: metadata?.get('ARROW:extension:name') === 'geoarrow.wkb' ? 'geometry' : 'blob' };
  }
  const element = type.children?.[0];
  if ((type.typeId === ARROW_LIST || type.typeId === ARROW_FIXED_SIZE_LIST) && element) {
    const items = shapeOf(element.type, element.metadata);
    return items && { items };
  }
  return undefined;
}

const BUNDLES: duckdb.DuckDBBundles = {
  mvp: { mainModule: mvpModule, mainWorker: mvpWorker },
  eh: { mainModule: ehModule, mainWorker: ehWorker },
};

export interface Session {
  duckdbVersion: string;
  variant: string;
  pgrVersion: string;
  query(sql: string): Promise<ResultSet>;
  // Makes bytes readable by SQL under `name` (read_csv, read_parquet), without a network request.
  registerFile(name: string, bytes: Uint8Array): Promise<void>;
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

function sqlString(text: string): string {
  return `'${text.replaceAll("'", "''")}'`;
}

function toResultSet(table: Awaited<ReturnType<duckdb.AsyncDuckDBConnection['query']>>): ResultSet {
  const fields = table.schema.fields;
  const columns = fields.map((f) => f.name);
  const shapes = fields.map((f) => shapeOf(f.type as unknown as ArrowType, f.metadata));
  const vectors = fields.map((_, i) => table.getChildAt(i));
  const rows = [];
  for (let r = 0; r < table.numRows; r++) rows.push(vectors.map((v, i) => plainValue(v?.get(r), shapes[i])));
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

  const pgrVersion = String((await query('SELECT pgr_version()')).rows[0]?.[0]);
  const registerFile = (name: string, bytes: Uint8Array) => db.registerFileBuffer(name, bytes);
  return { duckdbVersion, variant, pgrVersion, query, registerFile };
}
