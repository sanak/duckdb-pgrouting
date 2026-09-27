// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { GeographicMap } from '../src/datasets.ts';
import type { Session } from '../src/duckdb.ts';
import { networkOf } from '../src/loader.ts';
import type { Plain } from '../src/result.ts';

const MAP: GeographicMap = {
  mode: 'geographic',
  edges: 'SELECT edge_id AS id, ST_AsGeoJSON(geometry) AS geojson FROM pgr_edges',
  nodes: 'SELECT vertex_id AS id, ST_X(geometry) AS x, ST_Y(geometry) AS y FROM pgr_connectors',
  attribution: '© x',
};
const BUILT: GeographicMap = { ...MAP, dependsOn: ['pgr_connectors', 'pgr_edges'] };

function session(answer: (sql: string) => Plain[][]): Session {
  return {
    duckdbVersion: 'v1.5.5',
    variant: 'wasm_eh',
    pgrVersion: '4.0.1',
    query: async (sql: string) => ({ columns: [], rows: answer(sql) }),
    registerFile: async () => {},
  };
}

function failing(message: string): Session {
  return session(() => {
    throw new Error(message);
  });
}

const MISSING = 'Catalog Error: Table with name pgr_edges does not exist!\nDid you mean "pg_proc"?';

test('a network the presets have not built yet is empty, not an error', async () => {
  const g = await networkOf(failing(MISSING), BUILT);
  assert.equal(g.edges.features.length, 0);
  assert.equal(g.bounds, null);
});

test('a missing table is an error when the map does not depend on it', async () => {
  await assert.rejects(networkOf(failing(MISSING), MAP), /pgr_edges does not exist/);
  await assert.rejects(
    networkOf(failing('Catalog Error: Table with name edgez does not exist!'), BUILT),
    /edgez does not exist/,
  );
});

test('other errors still propagate', async () => {
  await assert.rejects(networkOf(failing('IO Error: Could not establish connection'), BUILT), /IO Error/);
});

test('built tables with no rows give an empty network', async () => {
  const g = await networkOf(
    session(() => []),
    BUILT,
  );
  assert.equal(g.bounds, null);
});

test('a built network is drawn from its rows', async () => {
  const g = await networkOf(
    session((sql) =>
      sql.includes('pgr_edges')
        ? [[1, '{"type":"LineString","coordinates":[[132.45,34.38],[132.46,34.39]]}']]
        : [[7, 132.45, 34.38]],
    ),
    BUILT,
  );
  assert.equal(g.edges.features.length, 1);
  assert.equal(g.nodes.features[0]?.properties.id, 7);
  assert.deepEqual(g.bounds, [
    [132.45, 34.38],
    [132.46, 34.39],
  ]);
});
