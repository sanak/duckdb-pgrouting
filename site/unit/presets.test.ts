// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { PRESETS } from '../src/presets.ts';

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
  'pgr_version',
];

test('ids are unique and every preset has SQL', () => {
  assert.equal(new Set(PRESETS.map((p) => p.id)).size, PRESETS.length);
  for (const p of PRESETS) assert.ok(p.sql.trim().length > 0, p.id);
});

test('every public function has at least one preset', () => {
  const called = new Set(PRESETS.flatMap((p) => [...p.sql.matchAll(/\b(pgr_\w+)\s*\(/g)].map((m) => m[1])));
  for (const name of FUNCTIONS) assert.ok(called.has(name), `no preset calls ${name}`);
});

test('the default preset is a one-to-one pgr_dijkstra', () => {
  assert.equal(PRESETS[0]?.id, 'dijkstra-one-to-one');
});
