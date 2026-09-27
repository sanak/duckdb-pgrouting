// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict';
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { test } from 'node:test';
import {
  attributionParts,
  createdNames,
  missingName,
  noteSource,
  type Preset,
  parsePresetFile,
  prerequisiteHint,
  presetGroups,
} from '../src/presets.ts';

const DATASETS = join(import.meta.dirname, '..', 'datasets');

// Only directories that actually hold a dataset (a stray .DS_Store, say, must not break this).
function datasetIds(): string[] {
  return readdirSync(DATASETS).filter((id) => existsSync(join(DATASETS, id, 'dataset.json')));
}

function presetFile(id: string) {
  const where = `${id}/presets.json`;
  return parsePresetFile(where, JSON.parse(readFileSync(join(DATASETS, id, 'presets.json'), 'utf8')));
}

// Every public function of the extension (duckdb_functions() rows tagged pgrouting_name).
const FUNCTIONS = [
  'pgr_dijkstra',
  'pgr_dijkstraCost',
  'pgr_dijkstraCostMatrix',
  'pgr_dijkstraNear',
  'pgr_dijkstraNearCost',
  'pgr_withPoints',
  'pgr_withPointsCost',
  'pgr_withPointsCostMatrix',
  'pgr_bdDijkstra',
  'pgr_bdDijkstraCost',
  'pgr_bdDijkstraCostMatrix',
  'pgr_bellmanFord',
  'pgr_edwardMoore',
  'pgr_dagShortestPath',
  'pgr_binaryBreadthFirstSearch',
  'pgr_connectedComponents',
  'pgr_extractVertices',
  'pgr_findCloseEdges',
  'pgr_version',
];

test('every committed presets.json parses, with unique ids', () => {
  for (const id of datasetIds()) {
    const file = presetFile(id);
    assert.ok(file.presets.length > 0, id);
    assert.equal(new Set(file.presets.map((p) => p.id)).size, file.presets.length, id);
  }
});

test('every public function has at least one sampledata preset', () => {
  const called = new Set(
    presetFile('sampledata').presets.flatMap((p) => [...p.sql.matchAll(/\b(pgr_\w+)\s*\(/g)].map((m) => m[1])),
  );
  for (const name of FUNCTIONS) assert.ok(called.has(name), `no preset calls ${name}`);
});

test('the default sampledata preset is a one-to-one pgr_dijkstra', () => {
  assert.equal(presetFile('sampledata').presets[0]?.id, 'dijkstra-one-to-one');
});

test('an array of lines is joined with newlines', () => {
  const file = parsePresetFile('t', {
    license: 'MIT',
    presets: [{ id: 'a', group: 'G', label: 'A', sql: ['SELECT 1', 'AS x;'] }],
  });
  assert.equal(file.presets[0]?.sql, 'SELECT 1\nAS x;');
});

test('bad preset files are rejected with the place that is wrong', () => {
  const ok = { id: 'a', group: 'G', label: 'A', sql: 'SELECT 1' };
  assert.throws(() => parsePresetFile('t', { presets: [ok] }), /t\.license/);
  assert.throws(() => parsePresetFile('t', { license: 'MIT', presets: [{ ...ok, id: 'A b' }] }), /presets\[0\]\.id/);
  assert.throws(() => parsePresetFile('t', { license: 'MIT', presets: [{ ...ok, sql: [] }] }), /presets\[0\]\.sql/);
  assert.throws(() => parsePresetFile('t', { license: 'MIT', presets: [{ ...ok, sql: [1] }] }), /presets\[0\]\.sql/);
  assert.throws(
    () => parsePresetFile('t', { license: 'MIT', presets: [{ ...ok, source: 'ftp://x' }] }),
    /presets\[0\]\.source/,
  );
  assert.throws(() => parsePresetFile('t', { license: 'X', licenseUrl: 'http://x', presets: [ok] }), /t\.licenseUrl/);
});

test('groups keep the order in which they first appear', () => {
  const p = (id: string, group: string): Preset => ({ id, group, label: id, sql: 'SELECT 1' });
  assert.deepEqual(
    presetGroups([p('a', 'X'), p('b', 'Y'), p('c', 'X')]).map((g) => [g.group, g.presets.map((q) => q.id)]),
    [
      ['X', ['a', 'c']],
      ['Y', ['b']],
    ],
  );
});

test('createdNames finds the tables, views and macros a preset creates', () => {
  assert.deepEqual(
    createdNames(`CREATE OR REPLACE VIEW vehicle_net AS SELECT 1;
create table IF NOT EXISTS "Walk_Net" AS SELECT 2;
CREATE OR REPLACE MACRO wrk_dijkstra(source, target) AS TABLE SELECT 3;
ALTER TABLE ways ADD COLUMN IF NOT EXISTS component BIGINT;`),
    ['vehicle_net', 'walk_net', 'wrk_dijkstra'],
  );
});

test('missingName reads DuckDB catalog errors for tables, views and table macros', () => {
  assert.equal(
    missingName('Catalog Error: Table with name walk_net does not exist!\nDid you mean "ways"?'),
    'walk_net',
  );
  assert.equal(missingName('Catalog Error: Table Function with name wrk_dijkstra does not exist!'), 'wrk_dijkstra');
  assert.equal(missingName('Binder Error: Referenced column "x" not found'), null);
});

test('prerequisiteHint names the preset that creates the missing name', () => {
  const presets: Preset[] = [
    { id: 'net', group: 'G', label: 'Create walk_net', sql: 'CREATE OR REPLACE TABLE walk_net AS SELECT 1;' },
    { id: 'route', group: 'G', label: 'Route', sql: "SELECT * FROM pgr_dijkstra('SELECT * FROM walk_net', 1, 2);" },
  ];
  const message = 'Catalog Error: Table with name walk_net does not exist!';
  assert.equal(prerequisiteHint(message, presets, 'route'), 'Run the preset “Create walk_net” first.');
  assert.equal(
    prerequisiteHint(message.replace('walk_net', 'WALK_NET'), presets, 'route'),
    'Run the preset “Create walk_net” first.',
  );
  // The preset that creates it is the one that failed: no hint.
  assert.equal(prerequisiteHint(message, presets, 'net'), null);
  // Nobody creates it, or it is another kind of error.
  assert.equal(prerequisiteHint('Catalog Error: Table with name nope does not exist!', presets, 'route'), null);
  assert.equal(prerequisiteHint('Parser Error: syntax error', presets, 'route'), null);
});

test('workshop presets carry the licence, the attribution and a chapter link each', () => {
  const file = presetFile('workshop-hiroshima');
  assert.equal(file.license, 'CC-BY-SA-3.0');
  assert.equal(file.licenseUrl, 'https://creativecommons.org/licenses/by-sa/3.0/');
  assert.match(file.attribution ?? '', /© pgRouting developers/);
  assert.match(file.attribution ?? '', /Changed for DuckDB/);
  for (const p of file.presets) {
    assert.match(p.source ?? '', /^https:\/\/workshop\.pgrouting\.org\/dev\/en\/basic\/\w+\.html$/, p.id);
    assert.match(p.group, /^[2-6] /, p.id);
  }
});

test('every preset that uses a table, view or macro comes after the one creating it', () => {
  // Comments may name what a later preset creates ("Next: …"); only the code counts.
  const code = (sql: string) => sql.replace(/--[^\n]*/g, '');
  for (const id of datasetIds()) {
    const presets = presetFile(id).presets;
    const created = new Map<string, number>();
    presets.forEach((p, i) => {
      for (const name of createdNames(p.sql)) if (!created.has(name)) created.set(name, i);
    });
    presets.forEach((p, i) => {
      for (const [name, at] of created) {
        if (at > i && new RegExp(`\\b${name}\\b`, 'i').test(code(p.sql)))
          assert.fail(`${id}: ${p.id} uses ${name}, created later`);
      }
    });
  }
});

test('sourceLabel is the text of the link to a preset source', () => {
  const one = { id: 'a', group: 'G', label: 'A', sql: 'SELECT 1' };
  assert.equal(
    parsePresetFile('t', { license: 'MIT', sourceLabel: 'overture.sql', presets: [one] }).sourceLabel,
    'overture.sql',
  );
  assert.equal(parsePresetFile('t', { license: 'MIT', presets: [one] }).sourceLabel, undefined);
  assert.throws(() => parsePresetFile('t', { license: 'MIT', sourceLabel: '', presets: [one] }), /t\.sourceLabel/);
});

const ARTICLE = 'https://www.crunchydata.com/blog/vehicle-routing-with-postgis-and-overture-data';

test('attributionParts splits [text](https://…) links out of the prose', () => {
  assert.deepEqual(attributionParts('Plain.'), ['Plain.']);
  assert.deepEqual(attributionParts('See [the page](https://example.org/a) and [b](https://x.test).'), [
    'See ',
    { text: 'the page', url: 'https://example.org/a' },
    ' and ',
    { text: 'b', url: 'https://x.test' },
    '.',
  ]);
  assert.throws(() => attributionParts('[x](http://example.org)'), /https:\/\//);
  assert.throws(() => attributionParts('a [dangling bracket'), /link/);
  assert.throws(() => attributionParts('a ] stray'), /link/);
});

test('a presets file with a broken attribution link is refused', () => {
  const file = { license: 'MIT', attribution: 'See [x](ftp://a).', presets: [] };
  assert.throws(() => parsePresetFile('x/presets.json', file), /x\/presets.json\.attribution/);
});

test('overture names its article as a link, and its SQL points at the same page', () => {
  const file = presetFile('overture');
  assert.deepEqual(
    attributionParts(file.attribution ?? '').find((part) => typeof part !== 'string'),
    { text: '“Vehicle Routing with PostGIS and Overture Data”', url: ARTICLE },
  );
  assert.ok(file.presets[0]?.sql.includes(`Overture Data": ${ARTICLE}`));
});

test('noteSource links a preset without a source of its own to the whole file', () => {
  const file = presetFile('overture');
  const [area, , find] = file.presets;
  assert.ok(area && find && area.source === undefined && find.source);
  assert.equal(noteSource(file, area), OVERTURE_SQL);
  assert.equal(noteSource(file, find), find.source);
  assert.equal(noteSource({ license: 'MIT', presets: [] }, area), undefined);
  // Every overture preset's note now carries an overture.sql link.
  assert.ok(file.presets.every((p) => noteSource(file, p)?.startsWith(OVERTURE_SQL)));
});

const OVERTURE_SQL =
  'https://github.com/CrunchyData/crunchy-bridge-for-analytics-examples/blob/fa3b6545e5cba0a44afdcd92d7995535ca20d979/overture/overture.sql';

test('overture presets carry the MIT notice, the attribution and pinned links', () => {
  const file = presetFile('overture');
  assert.equal(file.license, 'MIT');
  assert.equal(
    file.licenseUrl,
    'https://github.com/CrunchyData/crunchy-bridge-for-analytics-examples/blob/d09d90681a7eed3a9f49c3318d3181cdd17aff9f/LICENSE.md',
  );
  assert.match(file.attribution ?? '', /\(MIT, © 2024 Crunchy Data\)/);
  assert.match(file.attribution ?? '', /Changed for DuckDB:/);
  assert.equal(file.source, OVERTURE_SQL);
  assert.equal(file.sourceLabel, 'overture.sql');
  assert.deepEqual(
    file.presets.map((p) => p.id),
    [
      'area-bbox',
      'files-release',
      'files-find',
      'load-segments',
      'load-look',
      'prepare-macros',
      'prepare-segments',
      'prepare-connectors',
      'prepare-edges',
      'explore-components',
      'route-dijkstra',
      'route-geojson',
    ],
  );
  for (const p of file.presets) {
    assert.match(p.group, /^[1-6] /, p.id);
    if (p.source) assert.ok(p.source.startsWith(`${OVERTURE_SQL}#L`) && /#L\d+-L\d+$/.test(p.source), p.id);
  }
  assert.match(
    file.presets[0]?.sql.split('\n')[0] ?? '',
    /^-- Adapted from overture\.sql.*https:\/\/www\.crunchydata\.com\/blog\/vehicle-routing-with-postgis-and-overture-data$/,
  );
});
