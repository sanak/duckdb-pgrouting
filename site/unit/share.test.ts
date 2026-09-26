// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { decodeQuery, encodeQuery, hashFor, stateFromHash } from '../src/share.ts';

const SAMPLES = [
  '',
  'SELECT 1',
  "SELECT * FROM pgr_dijkstra('SELECT id, source, target, cost FROM edges', 5, 12)",
  '-- 経路\nSELECT 42 AS 答え;\n',
  'emoji 🚲 and tabs\tand + / = signs',
];

test('encode then decode returns the original SQL', () => {
  for (const sql of SAMPLES) assert.equal(decodeQuery(encodeQuery(sql)), sql);
});

test('the encoding is URL-safe and unpadded', () => {
  for (const sql of SAMPLES) assert.match(encodeQuery(sql), /^[A-Za-z0-9_-]*$/);
});

test('garbage decodes to null instead of throwing', () => {
  assert.equal(decodeQuery('not base64!'), null);
  assert.equal(decodeQuery('a+b/'), null); // standard, not URL-safe, alphabet
  assert.equal(decodeQuery('A'), null); // impossible length
  assert.equal(decodeQuery('_w'), null); // the single byte 0xFF is not UTF-8
});

test('a share link carries the dataset and the SQL', () => {
  assert.deepEqual(stateFromHash(hashFor('workshop-hiroshima', 'SELECT 1')), {
    dataset: 'workshop-hiroshima',
    sql: 'SELECT 1',
  });
  assert.ok(hashFor('sampledata', 'SELECT 1').startsWith('#d=sampledata&q='));
});

test('links from before datasets existed have no d and still open their SQL', () => {
  assert.deepEqual(stateFromHash(`#q=${encodeQuery('SELECT 2')}`), { dataset: null, sql: 'SELECT 2' });
});

test('stateFromHash ignores what it cannot use instead of throwing', () => {
  assert.deepEqual(stateFromHash(''), { dataset: null, sql: null });
  assert.deepEqual(stateFromHash('#x=1'), { dataset: null, sql: null });
  assert.deepEqual(stateFromHash('#q=%%%'), { dataset: null, sql: null });
  // A d that cannot be a dataset id is dropped; a well-formed unknown id is kept for main.ts to report.
  assert.deepEqual(stateFromHash(`#d=Bad%20Id&q=${encodeQuery('SELECT 3')}`), { dataset: null, sql: 'SELECT 3' });
  assert.deepEqual(stateFromHash('#d=no-such-dataset'), { dataset: 'no-such-dataset', sql: null });
});
