// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict';
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { test } from 'node:test';
import { parseDataset, parseDatasetIndex, quoteIdent, setupStatements } from '../src/datasets.ts';

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
  assert.throws(() => parseDataset('s', { ...good, tables: [] }), /tables: expected at least one/);
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
