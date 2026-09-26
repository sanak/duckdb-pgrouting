// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { EARTH_RADIUS, geographicGeometry, METRES_PER_UNIT, sampleGeometry, toLngLat } from '../src/geometry.ts';

// Web Mercator's forward projection, which MapLibre applies when it draws.
function forward([lng, lat]: [number, number]): [number, number] {
  const x = (EARTH_RADIUS * lng * Math.PI) / 180;
  const y = EARTH_RADIUS * Math.log(Math.tan(Math.PI / 4 + (lat * Math.PI) / 360));
  return [x / METRES_PER_UNIT, y / METRES_PER_UNIT];
}

test('the origin maps to 0,0', () => {
  assert.deepEqual(toLngLat(0, 0), [0, 0]);
});

test('MapLibre projection undoes toLngLat, so the grid is drawn undistorted', () => {
  for (const [x, y] of [
    [2, 3],
    [4, 1],
    [0.5, 4],
  ] as const) {
    const [fx, fy] = forward(toLngLat(x, y));
    assert.ok(Math.abs(fx - x) < 1e-9, `x ${fx} vs ${x}`);
    assert.ok(Math.abs(fy - y) < 1e-9, `y ${fy} vs ${y}`);
  }
});

const vertices = [
  { id: 1, x: 0, y: 0 },
  { id: 2, x: 2, y: 0 },
  { id: 3, x: 2, y: 2 },
];
const edges = [
  { id: 10, source: 1, target: 2 },
  { id: 11, source: 2, target: 3 },
  { id: 12, source: 3, target: 99 }, // dangling: no vertex 99
];

test('edges become lines between their vertices; dangling edges are skipped', () => {
  const g = sampleGeometry(vertices, edges, []);
  assert.deepEqual(
    g.edges.features.map((f) => f.properties.id),
    [10, 11],
  );
  assert.deepEqual(g.edges.features[0]?.geometry.coordinates, [toLngLat(0, 0), toLngLat(2, 0)]);
});

test('points sit at their fraction along the edge and carry the negative pid', () => {
  const g = sampleGeometry(vertices, edges, [
    { pid: 1, edge_id: 10, fraction: 0.25 },
    { pid: 2, edge_id: 404, fraction: 0.5 }, // unknown edge: skipped
  ]);
  const points = g.nodes.features.filter((f) => f.properties.kind === 'point');
  assert.equal(points.length, 1);
  assert.equal(points[0]?.properties.id, -1);
  assert.equal(points[0]?.properties.label, '-1');
  assert.deepEqual(points[0]?.geometry.coordinates, toLngLat(0.5, 0));
});

test('vertices are nodes labelled by id, and bounds cover every node', () => {
  const g = sampleGeometry(vertices, edges, []);
  assert.deepEqual(
    g.nodes.features.map((f) => [f.properties.id, f.properties.label, f.properties.kind]),
    [
      [1, '1', 'vertex'],
      [2, '2', 'vertex'],
      [3, '3', 'vertex'],
    ],
  );
  assert.deepEqual(g.bounds, [toLngLat(0, 0), toLngLat(2, 2)]);
});

test('geographicGeometry with nothing to draw throws a clear error', () => {
  assert.throws(() => geographicGeometry([], []), /nothing to draw/);
});

test('geographic edges come from GeoJSON LineStrings; anything else is skipped', () => {
  const g = geographicGeometry(
    [
      { id: 7, geojson: '{"type":"LineString","coordinates":[[132.45,34.39],[132.46,34.4]]}' },
      { id: 8, geojson: '{"type":"Point","coordinates":[132.45,34.39]}' },
      { id: 9, geojson: 'not json' },
    ],
    [],
  );
  assert.deepEqual(
    g.edges.features.map((f) => [f.properties.id, f.geometry.coordinates]),
    [
      [
        7,
        [
          [132.45, 34.39],
          [132.46, 34.4],
        ],
      ],
    ],
  );
});

test('geographic nodes are vertices at their longitude and latitude; bounds cover edges and nodes', () => {
  const g = geographicGeometry(
    [{ id: 7, geojson: '{"type":"LineString","coordinates":[[132.45,34.39],[132.46,34.4]]}' }],
    [{ id: 1688, x: 132.44, y: 34.395 }],
  );
  assert.deepEqual(
    g.nodes.features.map((f) => [f.properties.id, f.properties.kind, f.geometry.coordinates]),
    [[1688, 'vertex', [132.44, 34.395]]],
  );
  assert.deepEqual(g.bounds, [
    [132.44, 34.39],
    [132.46, 34.4],
  ]);
});
