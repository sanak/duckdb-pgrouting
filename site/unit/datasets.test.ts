// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict';
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { test } from 'node:test';
import {
  buildIndex,
  networkHint,
  parseDataset,
  parseDatasetIndex,
  quoteIdent,
  setupStatements,
} from '../src/datasets.ts';

const DATASETS = join(import.meta.dirname, '..', 'datasets');

// Only directories that actually hold a dataset (a stray .DS_Store, say, must not break this).
function committed(): string[] {
  return readdirSync(DATASETS)
    .filter((id) => existsSync(join(DATASETS, id, 'dataset.json')))
    .sort();
}

function read(id: string, file: string): unknown {
  return JSON.parse(readFileSync(join(DATASETS, id, file), 'utf8'));
}

test('every committed dataset.json parses', () => {
  assert.ok(committed().includes('sampledata'));
  for (const id of committed()) parseDataset(id, read(id, 'dataset.json'));
});

test('every file a table reads is listed, under <id>/', () => {
  for (const id of committed()) {
    const d = parseDataset(id, read(id, 'dataset.json'));
    for (const t of d.tables) {
      for (const m of t.sql.matchAll(/'([^']+\.(?:csv|parquet))'/g)) {
        const path = m[1] ?? '';
        assert.ok(path.startsWith(`${id}/`), `${id}.${t.name} reads ${path}`);
        assert.ok(d.files.includes(path.slice(id.length + 1)), `${id}.${t.name}: ${path} is not in files`);
      }
    }
  }
});

test('sampledata builds its five tables without spatial', () => {
  const d = parseDataset('sampledata', read('sampledata', 'dataset.json'));
  assert.equal(d.spatial, false);
  assert.equal(d.map.mode, 'abstract');
  assert.deepEqual(setupStatements(d).slice(0, 2), [
    'ATTACH IF NOT EXISTS \':memory:\' AS "sampledata"',
    'USE "sampledata"',
  ]);
  assert.deepEqual(
    d.tables.map((t) => t.name),
    ['edges', 'vertices', 'pointsofinterest', 'combinations', 'restrictions'],
  );
  assert.ok(!setupStatements(d).includes('LOAD spatial'));
});

test('a spatial dataset loads spatial right after USE', () => {
  const d = parseDataset('x-y', {
    title: 'X',
    license: 'MIT',
    spatial: true,
    files: ['a.parquet'],
    tables: [{ name: 'a', sql: "SELECT * FROM read_parquet('x-y/a.parquet')" }],
    map: { mode: 'geographic', edges: 'SELECT 1', nodes: 'SELECT 2', attribution: '© OSM' },
  });
  assert.deepEqual(setupStatements(d), [
    'ATTACH IF NOT EXISTS \':memory:\' AS "x-y"',
    'USE "x-y"',
    'LOAD spatial',
    'CREATE OR REPLACE TABLE "a" AS SELECT * FROM read_parquet(\'x-y/a.parquet\')',
  ]);
});

test('bad dataset files are rejected with the place that is wrong', () => {
  const good = read('sampledata', 'dataset.json') as Record<string, unknown>;
  assert.throws(() => parseDataset('Sample Data', good), /dataset id/);
  assert.throws(() => parseDataset('s', { ...good, spatial: 'yes' }), /spatial/);
  assert.throws(() => parseDataset('s', { ...good, files: ['../x.csv'] }), /files\[0\]/);
  assert.throws(() => parseDataset('s', { ...good, map: { mode: 'globe' } }), /map\.mode/);
  assert.throws(() => parseDataset('s', { ...good, title: '' }), /title/);
});

test('quoteIdent doubles embedded quotes', () => {
  assert.equal(quoteIdent('a"b'), '"a""b"');
});

test('the dataset index names a default that exists', () => {
  const index = parseDatasetIndex({
    default: 'b',
    datasets: [
      { id: 'a', title: 'A' },
      { id: 'b', title: 'B' },
    ],
  });
  assert.equal(index.default, 'b');
  assert.throws(() => parseDatasetIndex({ default: 'c', datasets: [{ id: 'a', title: 'A' }] }), /default/);
  assert.throws(() => parseDatasetIndex({ default: 'a', datasets: [] }), /datasets/);
});

test('workshop-hiroshima loads spatial and draws a geographic map', () => {
  const d = parseDataset('workshop-hiroshima', read('workshop-hiroshima', 'dataset.json'));
  assert.equal(d.spatial, true);
  assert.equal(d.map.mode, 'geographic');
  assert.deepEqual(
    d.tables.map((t) => t.name),
    ['ways', 'configuration'],
  );
  assert.ok(setupStatements(d).indexOf('LOAD spatial') < setupStatements(d).findIndex((s) => s.includes('ways')));
});

test('buildIndex puts the default first, then the rest by id', () => {
  const index = buildIndex([
    { id: 'zeta', title: 'Z' },
    { id: 'sampledata', title: 'S' },
    { id: 'alpha', title: 'A' },
  ]);
  assert.equal(index.default, 'sampledata');
  assert.deepEqual(
    index.datasets.map((d) => d.id),
    ['sampledata', 'alpha', 'zeta'],
  );
  assert.deepEqual(parseDatasetIndex(JSON.parse(JSON.stringify(index))), index);
  assert.throws(() => buildIndex([{ id: 'alpha', title: 'A' }]), /default/);
});

const GEOGRAPHIC = { mode: 'geographic', edges: 'SELECT 1', nodes: 'SELECT 2', attribution: '© OSM' };

function withMap(map: Record<string, unknown>, extra: Record<string, unknown> = {}): Record<string, unknown> {
  return { title: 'X', license: 'MIT', spatial: true, files: [], tables: [], map: { ...GEOGRAPHIC, ...map }, ...extra };
}

const EXTENT = { variable: 'bbox', kind: 'extent', label: 'Use this view', maxAreaKm2: 25 };
const POINT = { variable: 'pt0', kind: 'point', label: 'Pick start' };

test('a dataset may have no files and no tables', () => {
  const d = parseDataset('x-y', withMap({}));
  assert.deepEqual(d.files, []);
  assert.deepEqual(d.tables, []);
  assert.deepEqual(setupStatements(d), ['ATTACH IF NOT EXISTS \':memory:\' AS "x-y"', 'USE "x-y"', 'LOAD spatial']);
});

test('network is false unless the dataset says true', () => {
  assert.equal(parseDataset('x', withMap({})).network, false);
  assert.equal(parseDataset('x', withMap({}, { network: true })).network, true);
  assert.equal(parseDataset('sampledata', read('sampledata', 'dataset.json')).network, false);
  assert.throws(() => parseDataset('x', withMap({}, { network: 'yes' })), /x\/dataset\.json\.network/);
});

test('a geographic map may carry a view, dependsOn and inputs', () => {
  const d = parseDataset(
    'x',
    withMap({
      view: [132.44, 34.37, 132.48, 34.41],
      dependsOn: ['pgr_connectors', 'pgr_edges'],
      inputs: [EXTENT, POINT],
    }),
  );
  assert.ok(d.map.mode === 'geographic');
  assert.deepEqual(d.map.view, [132.44, 34.37, 132.48, 34.41]);
  assert.deepEqual(d.map.dependsOn, ['pgr_connectors', 'pgr_edges']);
  assert.deepEqual(d.map.inputs, [EXTENT, POINT]);
});

test('a geographic map without them has none of the three keys', () => {
  const d = parseDataset('workshop-hiroshima', read('workshop-hiroshima', 'dataset.json'));
  assert.ok(!('view' in d.map) && !('dependsOn' in d.map) && !('inputs' in d.map));
});

test('bad map inputs are rejected with the place that is wrong', () => {
  const bad = (inputs: unknown[]) => () => parseDataset('x', withMap({ inputs }));
  assert.throws(bad([{ ...POINT, kind: 'line' }]), /map\.inputs\[0\]\.kind/);
  assert.throws(bad([{ ...EXTENT, maxAreaKm2: undefined }]), /map\.inputs\[0\]\.maxAreaKm2/);
  assert.throws(bad([{ ...EXTENT, maxAreaKm2: 0 }]), /map\.inputs\[0\]\.maxAreaKm2/);
  assert.throws(bad([{ ...POINT, maxAreaKm2: 25 }]), /map\.inputs\[0\]\.maxAreaKm2/);
  assert.throws(bad([EXTENT, { ...EXTENT, variable: 'box2' }]), /map\.inputs: at most one extent/);
  assert.throws(bad([POINT, { ...POINT, label: 'Again' }]), /map\.inputs: each variable at most once/);
  assert.throws(bad([{ ...POINT, variable: 'Pt0' }]), /map\.inputs\[0\]\.variable/);
  assert.throws(bad([{ ...POINT, variable: '0pt' }]), /map\.inputs\[0\]\.variable/);
  assert.throws(bad([{ ...POINT, label: '' }]), /map\.inputs\[0\]\.label/);
  assert.throws(() => parseDataset('x', withMap({ inputs: 'bbox' })), /map\.inputs/);
});

test('a bad view or dependsOn is rejected', () => {
  const bad = (map: Record<string, unknown>) => () => parseDataset('x', withMap(map));
  assert.throws(bad({ view: [1, 2, 3] }), /map\.view/);
  assert.throws(bad({ view: [132.48, 34.37, 132.44, 34.41] }), /map\.view/);
  assert.throws(bad({ view: [0, 0, 181, 1] }), /map\.view/);
  assert.throws(bad({ view: [0, 0, '1', 1] }), /map\.view/);
  assert.throws(bad({ dependsOn: ['pgr_edges', 'Edges'] }), /map\.dependsOn\[1\]/);
  assert.throws(bad({ dependsOn: 'pgr_edges' }), /map\.dependsOn/);
});

test('networkHint speaks only for a network dataset and a connection error', () => {
  const network = parseDataset('x', withMap({}, { network: true }));
  const local = parseDataset('x', withMap({}));
  const hint = 'This dataset reads from third-party servers at run time; check the connection.';
  assert.equal(networkHint('IO Error: Could not establish connection', network), hint);
  assert.equal(networkHint("HTTP Error: HTTP GET error on 'https://x/y.json' (HTTP 403)", network), hint);
  assert.equal(networkHint('Catalog Error: Table with name ov_segments does not exist!', network), null);
  assert.equal(networkHint('IO Error: Could not establish connection', local), null);
});

test('overture is a network dataset with no files or tables and three map inputs', () => {
  const d = parseDataset('overture', read('overture', 'dataset.json'));
  assert.equal(d.network, true);
  assert.equal(d.spatial, true);
  assert.deepEqual([d.files, d.tables], [[], []]);
  assert.ok(d.map.mode === 'geographic');
  assert.deepEqual(d.map.dependsOn, ['pgr_connectors', 'pgr_edges']);
  assert.deepEqual(
    d.map.inputs?.map((i) => [i.variable, i.kind]),
    [
      ['bbox', 'extent'],
      ['pt0', 'point'],
      ['pt1', 'point'],
    ],
  );
});
