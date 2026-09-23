// SPDX-License-Identifier: GPL-2.0-or-later
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { bundledPgroutingVersion, DEFAULT_SITE_URL, extensionSource, LOCAL_TEMPLATE } from '../modes.mjs';

test('artifact is the default mode and is served locally', () => {
  assert.deepEqual(extensionSource({ W2_EXTENSION_DIR: '/tmp/ext' }), { mode: 'artifact', urlTemplate: LOCAL_TEMPLATE });
});

test('release mode is served locally too', () => {
  assert.deepEqual(extensionSource({ W2_MODE: 'release', W2_EXTENSION_DIR: '/tmp/ext' }), { mode: 'release', urlTemplate: LOCAL_TEMPLATE });
});

test('local modes need the extension directory', () => {
  assert.throws(() => extensionSource({ W2_MODE: 'release' }), /W2_EXTENSION_DIR/);
});

test('site mode uses the site wasm layout, default URL', () => {
  assert.deepEqual(extensionSource({ W2_MODE: 'site' }), {
    mode: 'site',
    urlTemplate: `${DEFAULT_SITE_URL}/wasm/{version}/{variant}/pgrouting.duckdb_extension.wasm`,
  });
});

test('site mode strips trailing slashes from a given URL', () => {
  assert.equal(
    extensionSource({ W2_MODE: 'site', W2_SITE_URL: 'http://localhost:5173/duckdb-pgrouting//' }).urlTemplate,
    'http://localhost:5173/duckdb-pgrouting/wasm/{version}/{variant}/pgrouting.duckdb_extension.wasm',
  );
});

test('community mode installs from the community repository', () => {
  assert.deepEqual(extensionSource({ W2_MODE: 'community' }), { mode: 'community', community: true });
});

test('an unknown mode is an error', () => {
  assert.throws(() => extensionSource({ W2_MODE: 'repository' }), /unknown W2_MODE/);
});

test('reads the pgRouting version from its CMakeLists', () => {
  assert.equal(bundledPgroutingVersion('cmake_minimum_required(VERSION 3.12)\nproject(PGROUTING VERSION 4.0.2\n'), '4.0.2');
  assert.throws(() => bundledPgroutingVersion('project(OTHER VERSION 1.0.0)\n'), /PGROUTING VERSION/);
});
