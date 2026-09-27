// SPDX-License-Identifier: MIT
// A dataset is a folder of data files and a dataset.json that says how to turn them into tables and
// how to draw them. Each dataset lives in its own in-memory catalog named after its id, so two
// datasets can both have a `vertices` table. The table SQL names files as "<id>/<file>": the page
// registers each fetched file under that name, and the native preset test runs from test/data.
// A dataset may also have no files and no tables at all and build everything with its presets
// from data other servers hold (`network: true`); its map then draws tables those presets create
// (`dependsOn`), opens on `view` while they do not exist yet, and offers `inputs` that rewrite the
// presets' SET VARIABLE lines (inputs.ts).
import { type Json, list, object, text } from './json.ts';

export interface TableSpec {
  name: string;
  sql: string;
}

// The sample graph's grid coordinates, drawn on a blank background (see geometry.ts).
export interface AbstractMap {
  mode: 'abstract';
  vertices: string; // id, x, y
  edges: string; // id, source, target
  points: string; // pid, edge_id, fraction
}

// xmin, ymin, xmax, ymax in longitude/latitude.
export type View = [number, number, number, number];

// A map button whose value lives in the SQL as `SET VARIABLE <variable> = …;` (inputs.ts): an
// extent takes the map's view, a point is picked by clicking the map.
export interface MapInput {
  variable: string;
  kind: 'extent' | 'point';
  label: string;
  maxAreaKm2?: number; // an extent's, and only an extent's
}

// Longitude/latitude data over a basemap.
export interface GeographicMap {
  mode: 'geographic';
  edges: string; // id, geojson (a LineString)
  nodes: string; // id, x, y
  attribution: string;
  view?: View; // where the map opens while the network is empty
  dependsOn?: string[]; // lower-case names of the tables the edges and nodes queries read
  inputs?: MapInput[];
}

export type MapSpec = AbstractMap | GeographicMap;

export interface Dataset {
  id: string;
  title: string;
  license: string; // the licence of this dataset.json file itself, not of the data it describes
  spatial: boolean;
  network: boolean; // its presets read from third-party servers at the reader's request
  files: string[];
  tables: TableSpec[];
  map: MapSpec;
}

export interface DatasetIndex {
  default: string;
  datasets: { id: string; title: string }[];
}

export const DATASET_ID = /^[a-z0-9]+(?:-[a-z0-9]+)*$/;
// A lower-case SQL name: a variable of an input, a table of dependsOn.
export const SQL_NAME = /^[a-z_][a-z0-9_]*$/;
const FILE_NAME = /^[A-Za-z0-9_][A-Za-z0-9_.-]*$/;

function optionalView(o: Json, where: string): View | undefined {
  if (o.view === undefined) return undefined;
  const v = o.view;
  if (!Array.isArray(v) || v.length !== 4 || !v.every((n) => typeof n === 'number' && Number.isFinite(n)))
    throw new Error(`${where}.view: expected [xmin, ymin, xmax, ymax]`);
  const [xmin, ymin, xmax, ymax] = v as View;
  if (!(xmin < xmax && ymin < ymax && xmin >= -180 && xmax <= 180 && ymin >= -90 && ymax <= 90))
    throw new Error(`${where}.view: expected xmin < xmax and ymin < ymax, in longitude/latitude`);
  return [xmin, ymin, xmax, ymax];
}

function optionalDependsOn(o: Json, where: string): string[] | undefined {
  if (o.dependsOn === undefined) return undefined;
  return list(o, 'dependsOn', where).map((name, i) => {
    if (typeof name !== 'string' || !SQL_NAME.test(name))
      throw new Error(`${where}.dependsOn[${i}]: expected a lower-case table name`);
    return name;
  });
}

function parseInput(value: unknown, where: string): MapInput {
  const o = object(value, where);
  const variable = text(o, 'variable', where);
  if (!SQL_NAME.test(variable)) throw new Error(`${where}.variable: '${variable}' must be a lower-case SQL name`);
  const label = text(o, 'label', where);
  if (o.kind === 'point') {
    if (o.maxAreaKm2 !== undefined) throw new Error(`${where}.maxAreaKm2: only an extent has one`);
    return { variable, kind: 'point', label };
  }
  if (o.kind === 'extent') {
    const max = o.maxAreaKm2;
    if (typeof max !== 'number' || !Number.isFinite(max) || max <= 0)
      throw new Error(`${where}.maxAreaKm2: expected a positive number`);
    return { variable, kind: 'extent', label, maxAreaKm2: max };
  }
  throw new Error(`${where}.kind: expected 'extent' or 'point'`);
}

function optionalInputs(o: Json, where: string): MapInput[] | undefined {
  if (o.inputs === undefined) return undefined;
  const inputs = list(o, 'inputs', where).map((v, i) => parseInput(v, `${where}.inputs[${i}]`));
  if (inputs.filter((input) => input.kind === 'extent').length > 1)
    throw new Error(`${where}.inputs: at most one extent`);
  if (new Set(inputs.map((input) => input.variable)).size !== inputs.length)
    throw new Error(`${where}.inputs: each variable at most once`);
  return inputs;
}

function parseMap(o: Json, where: string): MapSpec {
  if (o.mode === 'abstract') {
    return {
      mode: 'abstract',
      vertices: text(o, 'vertices', where),
      edges: text(o, 'edges', where),
      points: text(o, 'points', where),
    };
  }
  if (o.mode === 'geographic') {
    const view = optionalView(o, where);
    const dependsOn = optionalDependsOn(o, where);
    const inputs = optionalInputs(o, where);
    return {
      mode: 'geographic',
      edges: text(o, 'edges', where),
      nodes: text(o, 'nodes', where),
      attribution: text(o, 'attribution', where),
      ...(view ? { view } : {}),
      ...(dependsOn ? { dependsOn } : {}),
      ...(inputs ? { inputs } : {}),
    };
  }
  throw new Error(`${where}.mode: expected 'abstract' or 'geographic'`);
}

function flag(o: Json, key: string, where: string): boolean {
  const value = o[key] ?? false;
  if (typeof value !== 'boolean') throw new Error(`${where}.${key}: expected true or false`);
  return value;
}

export function parseDataset(id: string, value: unknown): Dataset {
  if (!DATASET_ID.test(id)) throw new Error(`dataset id '${id}' must be lower-case words joined by '-'`);
  const where = `${id}/dataset.json`;
  const o = object(value, where);
  const files = list(o, 'files', where).map((f, i) => {
    if (typeof f !== 'string' || !FILE_NAME.test(f))
      throw new Error(`${where}.files[${i}]: expected a plain file name`);
    return f;
  });
  const tables = list(o, 'tables', where).map((t, i) => {
    const w = `${where}.tables[${i}]`;
    const table = object(t, w);
    return { name: text(table, 'name', w), sql: text(table, 'sql', w) };
  });
  return {
    id,
    title: text(o, 'title', where),
    license: text(o, 'license', where),
    spatial: flag(o, 'spatial', where),
    network: flag(o, 'network', where),
    files,
    tables,
    map: parseMap(object(o.map, `${where}.map`), `${where}.map`),
  };
}

// A network dataset's queries fail with DuckDB's own HTTP or IO error when a server cannot be
// reached; this adds what the reader can do about it.
export function networkHint(message: string, dataset: Dataset): string | null {
  if (!dataset.network || !/\bHTTP\b|IO Error/.test(message)) return null;
  return 'This dataset reads from third-party servers at run time; check the connection.';
}

export function parseDatasetIndex(value: unknown): DatasetIndex {
  const where = 'data/index.json';
  const o = object(value, where);
  const datasets = list(o, 'datasets', where).map((d, i) => {
    const w = `${where}.datasets[${i}]`;
    const entry = object(d, w);
    return { id: text(entry, 'id', w), title: text(entry, 'title', w) };
  });
  if (datasets.length === 0) throw new Error(`${where}.datasets: expected at least one dataset`);
  const fallback = text(o, 'default', where);
  if (!datasets.some((d) => d.id === fallback)) throw new Error(`${where}.default: '${fallback}' is not listed`);
  return { default: fallback, datasets };
}

// Mirrored by scripts/tests/test_site_presets.py's quote_ident() for the native preset test
// (stdlib-only Python cannot import TypeScript); keep the two in step.
export function quoteIdent(name: string): string {
  return `"${name.replaceAll('"', '""')}"`;
}

export function useStatement(id: string): string {
  return `USE ${quoteIdent(id)}`;
}

// The statements that build a dataset in its own catalog, in order; the catalog stays selected.
// They can run again after a failure part-way (spatial could not be downloaded, say).
// Mirrored by scripts/tests/test_site_presets.py's setup_sql() for the native preset test
// (stdlib-only Python cannot import TypeScript); keep the two in step.
export function setupStatements(d: Dataset): string[] {
  return [
    `ATTACH IF NOT EXISTS ':memory:' AS ${quoteIdent(d.id)}`,
    useStatement(d.id),
    ...(d.spatial ? ['LOAD spatial'] : []),
    ...d.tables.map((t) => `CREATE OR REPLACE TABLE ${quoteIdent(t.name)} AS ${t.sql}`),
  ];
}

// The index collect.ts writes to public/data/index.json: the default dataset first, then by id.
export function buildIndex(entries: { id: string; title: string }[], fallback = 'sampledata'): DatasetIndex {
  const datasets = [...entries].sort((a, b) =>
    a.id === fallback ? -1 : b.id === fallback ? 1 : a.id.localeCompare(b.id),
  );
  return parseDatasetIndex({ default: fallback, datasets });
}
