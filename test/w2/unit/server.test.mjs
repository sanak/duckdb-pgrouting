// SPDX-License-Identifier: GPL-2.0-or-later
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { resolvePath } from '../server.mjs';

const roots = { files: { '/': '/w2/index.html' }, prefixes: [['/ext/', '/tmp/ext'], ['/data/', '/repo/test/data/sampledata']] };

test('exact files and prefixed files resolve', () => {
  assert.equal(resolvePath('/', roots), '/w2/index.html');
  assert.equal(resolvePath('/ext/wasm_eh/pgrouting.duckdb_extension.wasm', roots), '/tmp/ext/wasm_eh/pgrouting.duckdb_extension.wasm');
  assert.equal(resolvePath('/data/edges.csv', roots), '/repo/test/data/sampledata/edges.csv');
});

test('paths outside a root do not resolve', () => {
  assert.equal(resolvePath('/ext/../../etc/passwd', roots), null);
  assert.equal(resolvePath('/ext/%2e%2e/%2e%2e/etc/passwd', roots), null);
  assert.equal(resolvePath('/nowhere', roots), null);
});

test('a prefix without a directory does not resolve', () => {
  assert.equal(resolvePath('/ext/x.wasm', { files: {}, prefixes: [['/ext/', null]] }), null);
});

test('a malformed escape does not resolve', () => {
  assert.equal(resolvePath('/ext/%E0%A4%A', roots), null);
});
