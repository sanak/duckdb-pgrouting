// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { newestPerTarget, parseAssetName, type WasmAsset } from '../scripts/assets.ts';
import { extensionPath } from '../src/paths.ts';

test('extensionPath keeps the layout W2 site mode expects', () => {
  assert.equal(extensionPath('v1.5.5', 'wasm_eh'), 'wasm/v1.5.5/wasm_eh/pgrouting.duckdb_extension.wasm');
});

test('parseAssetName reads version, target and kind without splitting on dots', () => {
  assert.deepEqual(parseAssetName('pgrouting.v1.5.5.wasm_eh.duckdb_extension.wasm'), {
    duckdbVersion: 'v1.5.5',
    target: 'wasm_eh',
    kind: 'wasm',
  });
  assert.deepEqual(parseAssetName('pgrouting.v1.5.5.osx_arm64.duckdb_extension.gz'), {
    duckdbVersion: 'v1.5.5',
    target: 'osx_arm64',
    kind: 'gz',
  });
  assert.equal(parseAssetName('SHA256SUMS'), null);
  assert.equal(parseAssetName('pgrouting.v1.5.wasm_eh.duckdb_extension.wasm'), null);
  assert.equal(parseAssetName('other.v1.5.5.wasm_eh.duckdb_extension.wasm'), null);
});

const asset = (tag: string, publishedAt: string, duckdbVersion: string, variant: string): WasmAsset => ({
  tag,
  publishedAt,
  name: `pgrouting.${duckdbVersion}.${variant}.duckdb_extension.wasm`,
  duckdbVersion,
  variant,
});

test('the newest release wins per (DuckDB version, variant); older versions stay', () => {
  const chosen = newestPerTarget([
    asset('v0.1.0', '2026-09-24T11:53:19Z', 'v1.5.5', 'wasm_eh'),
    asset('v0.1.1', '2026-09-29T00:00:00Z', 'v1.5.6', 'wasm_eh'),
    asset('v0.1.2', '2026-10-05T00:00:00Z', 'v1.5.5', 'wasm_eh'),
    asset('v0.1.0', '2026-09-24T11:53:19Z', 'v1.5.5', 'wasm_mvp'),
  ]);
  assert.deepEqual(
    chosen.map((a) => [a.duckdbVersion, a.variant, a.tag]),
    [
      ['v1.5.5', 'wasm_eh', 'v0.1.2'],
      ['v1.5.5', 'wasm_mvp', 'v0.1.0'],
      ['v1.5.6', 'wasm_eh', 'v0.1.1'],
    ],
  );
});

test('no assets, no selection', () => {
  assert.deepEqual(newestPerTarget([]), []);
});
