// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { idFilter, PATH_COLOURS, pathColour } from '../src/expressions.ts';

test('idFilter matches exactly the given ids, and nothing when empty', () => {
  assert.deepEqual(
    idFilter(
      new Map([
        [4, 0],
        [7, 1],
      ]),
    ),
    ['in', ['get', 'id'], ['literal', [4, 7]]],
  );
  assert.deepEqual(idFilter(new Map()), ['in', ['get', 'id'], ['literal', []]]);
});

test('pathColour colours each id by its path, cycling the palette', () => {
  const byId = new Map([
    [4, 0],
    [7, PATH_COLOURS.length],
  ]);
  assert.deepEqual(pathColour(byId), ['match', ['get', 'id'], 4, PATH_COLOURS[0], 7, PATH_COLOURS[0], PATH_COLOURS[0]]);
});

test('pathColour of nothing is a plain colour (match needs at least one label)', () => {
  assert.equal(pathColour(new Map()), PATH_COLOURS[0]);
});
