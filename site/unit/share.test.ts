// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { decodeQuery, encodeQuery, hashForQuery, queryFromHash } from '../src/share.ts';

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

test('queryFromHash reads q and ignores everything else', () => {
  assert.equal(queryFromHash(hashForQuery('SELECT 1')), 'SELECT 1');
  assert.equal(queryFromHash(`#x=1&q=${encodeQuery('SELECT 2')}`), 'SELECT 2');
  assert.equal(queryFromHash(''), null);
  assert.equal(queryFromHash('#x=1'), null);
  assert.equal(queryFromHash('#q=%%%'), null);
});

test('hashForQuery starts with #q=', () => {
  assert.ok(hashForQuery('SELECT 1').startsWith('#q='));
});
