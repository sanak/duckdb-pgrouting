// SPDX-License-Identifier: MIT
// The site and W2 must run the same DuckDB-Wasm: W2's site mode vouches for what the site serves.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';

function duckdbWasmPin(relativePath: string): string | undefined {
  const pkg = JSON.parse(readFileSync(new URL(relativePath, import.meta.url), 'utf8')) as {
    dependencies?: Record<string, string>;
  };
  return pkg.dependencies?.['@duckdb/duckdb-wasm'];
}

test('the site pins DuckDB-Wasm exactly', () => {
  assert.match(duckdbWasmPin('../package.json') ?? '', /^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$/);
});

test('the site and W2 pin the same DuckDB-Wasm', () => {
  assert.equal(duckdbWasmPin('../package.json'), duckdbWasmPin('../../test/w2/package.json'));
});
