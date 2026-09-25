// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { nextSelection } from '../src/selection.ts';

test('a grid pick selects its edge, even the one already selected', () => {
  assert.equal(nextSelection(null, 7, 'grid'), 7);
  assert.equal(nextSelection(4, 7, 'grid'), 7);
  assert.equal(nextSelection(7, 7, 'grid'), 7);
});

test('a map pick selects its edge, and picking the selected edge again clears it', () => {
  assert.equal(nextSelection(null, 7, 'map'), 7);
  assert.equal(nextSelection(4, 7, 'map'), 7);
  assert.equal(nextSelection(7, 7, 'map'), null);
});

test('picking nothing selectable clears the selection from either side', () => {
  assert.equal(nextSelection(7, null, 'grid'), null);
  assert.equal(nextSelection(7, null, 'map'), null);
  assert.equal(nextSelection(null, null, 'map'), null);
});

test('edge 0 is an edge like any other', () => {
  assert.equal(nextSelection(null, 0, 'map'), 0);
  assert.equal(nextSelection(0, 0, 'map'), null);
});
