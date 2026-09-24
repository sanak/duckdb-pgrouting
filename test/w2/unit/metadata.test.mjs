// SPDX-License-Identifier: GPL-2.0-or-later
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { readFooter } from '../metadata.mjs';

// A build's last 512 bytes, laid out as DuckDB writes them: eight 32-byte fields, last field first,
// then a 256-byte signature (zeros when unsigned).
function footer(fields, bodyLength = 100) {
  const body = Buffer.alloc(bodyLength, 0xab);
  const metadata = Buffer.alloc(256);
  [...fields].reverse().forEach((value, i) => metadata.write(value, i * 32, 'latin1'));
  return Buffer.concat([body, metadata, Buffer.alloc(256)]);
}

test('reads the fields DuckDB parses from a build', () => {
  assert.deepEqual(readFooter(footer(['4', 'wasm_eh', 'v1.5.5', 'v0.1.0', 'CPP', '', '', ''])), {
    platform: 'wasm_eh',
    duckdbVersion: 'v1.5.5',
    extensionVersion: 'v0.1.0',
    abi: 'CPP',
  });
});

test('a file without the magic value is not a DuckDB extension', () => {
  assert.throws(() => readFooter(footer(['3', 'wasm_eh', 'v1.5.5', 'v0.1.0', 'CPP', '', '', ''])), /magic/);
});

test('a file shorter than the footer is not a DuckDB extension', () => {
  assert.throws(() => readFooter(Buffer.alloc(511)), /512/);
});
