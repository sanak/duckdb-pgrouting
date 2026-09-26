// SPDX-License-Identifier: MIT
// A dataset is a folder of data files and a dataset.json that says how to turn them into tables and
// how to draw them. Each dataset lives in its own in-memory catalog named after its id, so two
// datasets can both have a `vertices` table. The table SQL names files as "<id>/<file>": the page
// registers each fetched file under that name, and the native preset test runs from test/data.
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

// Longitude/latitude data over a basemap.
export interface GeographicMap {
  mode: 'geographic';
  edges: string; // id, geojson (a LineString)
  nodes: string; // id, x, y
  attribution: string;
}

export type MapSpec = AbstractMap | GeographicMap;

export interface Dataset {
  id: string;
  title: string;
  license: string; // the licence of this dataset.json file itself, not of the data it describes
  spatial: boolean;
  files: string[];
  tables: TableSpec[];
  map: MapSpec;
}

export interface DatasetIndex {
  default: string;
  datasets: { id: string; title: string }[];
}

export const DATASET_ID = /^[a-z0-9]+(?:-[a-z0-9]+)*$/;
const FILE_NAME = /^[A-Za-z0-9_][A-Za-z0-9_.-]*$/;

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
    return {
      mode: 'geographic',
      edges: text(o, 'edges', where),
      nodes: text(o, 'nodes', where),
      attribution: text(o, 'attribution', where),
    };
  }
  throw new Error(`${where}.mode: expected 'abstract' or 'geographic'`);
}

export function parseDataset(id: string, value: unknown): Dataset {
  if (!DATASET_ID.test(id)) throw new Error(`dataset id '${id}' must be lower-case words joined by '-'`);
  const where = `${id}/dataset.json`;
  const o = object(value, where);
  const spatial = o.spatial ?? false;
  if (typeof spatial !== 'boolean') throw new Error(`${where}.spatial: expected true or false`);
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
  if (tables.length === 0) throw new Error(`${where}.tables: expected at least one table`);
  return {
    id,
    title: text(o, 'title', where),
    license: text(o, 'license', where),
    spatial,
    files,
    tables,
    map: parseMap(object(o.map, `${where}.map`), `${where}.map`),
  };
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
