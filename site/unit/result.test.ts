// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { formatCell, highlightOf, type Plain, plainValue, type ResultSet, summarize } from '../src/result.ts';

test('plainValue turns Arrow values into plain JS', () => {
  assert.equal(plainValue(12n), 12);
  assert.equal(plainValue(undefined), null);
  assert.equal(plainValue('x'), 'x');
  // An Arrow list arrives as a Vector with toArray(); its elements may be bigints.
  assert.deepEqual(plainValue({ toArray: () => BigInt64Array.from([1n, 2n]) }), [1, 2]);
  // An Arrow DECIMAL arrives unscaled; the field's scale restores it (SELECT 1.5 → 15, scale 1).
  const unscaled = { [Symbol.toPrimitive]: () => 15 };
  assert.equal(plainValue(unscaled, 1), 1.5);
  assert.equal(plainValue(null, 1), null);
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
