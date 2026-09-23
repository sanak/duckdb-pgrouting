// SPDX-License-Identifier: GPL-2.0-or-later
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { layout, variantOf } from '../prepare-extension.mjs';

test('a CI artifact directory gives the variant', () => {
  assert.equal(variantOf('pgrouting-v1.5.5-extension-wasm_eh/pgrouting.duckdb_extension.wasm'), 'wasm_eh');
});

test('a release asset gives the variant despite the dots in the DuckDB version', () => {
  assert.equal(variantOf('pgrouting.v1.5.5.wasm_threads.duckdb_extension.wasm'), 'wasm_threads');
});

test('native files and unrelated files are ignored', () => {
  assert.equal(variantOf('pgrouting.v1.5.5.linux_amd64.duckdb_extension.gz'), null);
  assert.equal(variantOf('pgrouting-v1.5.5-extension-linux_amd64/pgrouting.duckdb_extension'), null);
  assert.equal(variantOf('SHA256SUMS'), null);
});

test('layout maps each variant to its file', () => {
  const plan = layout([
    'pgrouting.v1.5.5.wasm_eh.duckdb_extension.wasm',
    'pgrouting.v1.5.5.wasm_mvp.duckdb_extension.wasm',
    'SHA256SUMS',
  ]);
  assert.deepEqual([...plan.entries()].sort(), [
    ['wasm_eh', 'pgrouting.v1.5.5.wasm_eh.duckdb_extension.wasm'],
    ['wasm_mvp', 'pgrouting.v1.5.5.wasm_mvp.duckdb_extension.wasm'],
  ]);
});

test('two files for one variant is an error', () => {
  assert.throws(
    () => layout(['pgrouting.v1.5.5.wasm_eh.duckdb_extension.wasm', 'pgrouting.v1.5.6.wasm_eh.duckdb_extension.wasm']),
    /wasm_eh/,
  );
});
