// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict';
import { test } from 'node:test';
import {
  edgeOf,
  formatCell,
  gridData,
  highlightOf,
  type Plain,
  plainValue,
  type ResultSet,
  summarize,
} from '../src/result.ts';

test('plainValue turns Arrow values into plain JS', () => {
  assert.equal(plainValue(12n), 12);
  assert.equal(plainValue(undefined), null);
  assert.equal(plainValue('x'), 'x');
  // An Arrow list arrives as a Vector with toArray(); its elements may be bigints.
  assert.deepEqual(plainValue({ toArray: () => BigInt64Array.from([1n, 2n]) }), [1, 2]);
  // An Arrow DECIMAL arrives unscaled; the field's scale restores it (SELECT 1.5 → 15, scale 1).
  const unscaled = { [Symbol.toPrimitive]: () => 15 };
  assert.equal(plainValue(unscaled, { scale: 1 }), 1.5);
  assert.equal(plainValue(null, { scale: 1 }), null);
});

test('plainValue scales DECIMAL elements inside lists (SELECT [1.5, 2.5], [[0.5]])', () => {
  const unscaled = (n: number) => ({ [Symbol.toPrimitive]: () => n });
  // Like an Arrow Vector of DECIMAL: iterating yields one value per element, but toArray() returns
  // the raw 128-bit storage, four 32-bit words per value.
  const list = (items: unknown[]) => ({
    toArray: () => Uint32Array.from(items.flatMap((v) => [Number(v), 0, 0, 0])),
    [Symbol.iterator]: () => items[Symbol.iterator](),
  });
  assert.deepEqual(plainValue(list([unscaled(15), unscaled(25)]), { items: { scale: 1 } }), [1.5, 2.5]);
  assert.deepEqual(plainValue(list([list([unscaled(5)])]), { items: { items: { scale: 1 } } }), [[0.5]]);
});

test('formatCell renders SQL-like text and never escapes', () => {
  assert.equal(formatCell(null), 'NULL');
  assert.equal(formatCell([1, 2]), '[1, 2]');
  assert.equal(formatCell(true), 'true');
  assert.equal(formatCell('<img src=x onerror=alert(1)>'), '<img src=x onerror=alert(1)>');
});

test('summarize caps the table and reports the true total', () => {
  const rows = Array.from({ length: 1234 }, (_, i) => [i]);
  const { shown, text } = summarize({ columns: ['i'], rows }, 1000);
  assert.equal(shown.length, 1000);
  assert.equal(text, '1,234 rows (first 1,000 shown)');
  assert.equal(summarize({ columns: ['i'], rows: [[1]] }, 1000).text, '1 row');
  assert.equal(summarize({ columns: ['i'], rows: [] }, 1000).text, '0 rows');
});

const path = (columns: string[], rows: Plain[][]): ResultSet => ({ columns, rows });

test('one path: every edge and node gets path 0, edge -1 is skipped', () => {
  const h = highlightOf(
    path(
      ['seq', 'start_vid', 'end_vid', 'node', 'edge'],
      [
        [1, 5, 12, 5, 1],
        [2, 5, 12, 6, 4],
        [3, 5, 12, 12, -1],
      ],
    ),
  );
  assert.deepEqual(
    [...h.edges],
    [
      [1, 0],
      [4, 0],
    ],
  );
  assert.deepEqual(
    [...h.nodes],
    [
      [5, 0],
      [6, 0],
      [12, 0],
    ],
  );
});

test('paths are keyed by (start_vid, end_vid); a shared edge keeps its first path', () => {
  const h = highlightOf(
    path(
      ['start_vid', 'end_vid', 'node', 'edge'],
      [
        [5, 11, 5, 1],
        [5, 11, 11, -1],
        [5, 12, 5, 1],
        [5, 12, 12, -1],
      ],
    ),
  );
  assert.deepEqual([...h.edges], [[1, 0]]);
  assert.deepEqual(
    [...h.nodes],
    [
      [5, 0],
      [11, 0],
      [12, 1],
    ],
  );
});

test('path_id wins over start_vid/end_vid', () => {
  const h = highlightOf(
    path(
      ['path_id', 'start_vid', 'end_vid', 'node', 'edge'],
      [
        [1, 5, 12, 5, 1],
        [2, 5, 12, 5, 2],
      ],
    ),
  );
  assert.deepEqual(
    [...h.edges],
    [
      [1, 0],
      [2, 1],
    ],
  );
});

test('negative point nodes are kept, null and negative edges are not', () => {
  const h = highlightOf(
    path(
      ['node', 'edge'],
      [
        [-1, 1],
        [-6, null],
        [7, -2],
      ],
    ),
  );
  assert.deepEqual([...h.edges], [[1, 0]]);
  assert.deepEqual(
    [...h.nodes],
    [
      [-1, 0],
      [-6, 0],
      [7, 0],
    ],
  );
});

test('a result without edge or node columns highlights nothing', () => {
  const h = highlightOf(path(['start_vid', 'end_vid', 'agg_cost'], [[5, 12, 4]]));
  assert.equal(h.edges.size, 0);
  assert.equal(h.nodes.size, 0);
});

test('gridData keys columns by position and keeps every SQL column name as the title', () => {
  const { columns } = gridData({ columns: ['a.b', 'a.b', '<b>x</b>'], rows: [] }, []);
  assert.deepEqual(columns, [
    { title: 'a.b', field: 'c0' },
    { title: 'a.b', field: 'c1' },
    { title: '<b>x</b>', field: 'c2' },
  ]);
});

test('gridData cells hold the text the page shows, which is also what is copied', () => {
  const result = { columns: ['seq', 'edge', 'path', 'note'], rows: [[1, -1, [1, 2], null]] };
  assert.deepEqual(gridData(result, result.rows).rows, [{ c0: '1', c1: '-1', c2: '[1, 2]', c3: 'NULL' }]);
});

test('gridData renders only the rows it is given (the capped ones)', () => {
  const result = { columns: ['i'], rows: [[1], [2], [3]] };
  assert.deepEqual(gridData(result, result.rows.slice(0, 2)).rows, [{ c0: '1' }, { c0: '2' }]);
});

test('gridData of a zero-column result has no columns and no rows', () => {
  assert.deepEqual(gridData({ columns: [], rows: [] }, []), { columns: [], rows: [] });
});

const route: ResultSet = { columns: ['seq', 'node', 'edge', 'cost'], rows: [] };

test('edgeOf returns the edge id of a path row', () => {
  assert.equal(edgeOf(route, [1, 5, 7, 1]), 7);
  assert.equal(edgeOf(route, [1, 5, 0, 1]), 0);
});

test('edgeOf selects nothing for the final row of a path, NULL, or a result without edges', () => {
  assert.equal(edgeOf(route, [3, 12, -1, 0]), null);
  assert.equal(edgeOf(route, [3, 12, null, 0]), null);
  assert.equal(edgeOf({ columns: ['start_vid', 'end_vid', 'agg_cost'], rows: [] }, [5, 12, 4]), null);
});

test('edgeOf ignores an edge column that holds text', () => {
  assert.equal(edgeOf({ columns: ['edge'], rows: [] }, ['7']), null);
});
