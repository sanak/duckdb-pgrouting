// SPDX-License-Identifier: GPL-2.0-or-later
// W2: the extension's Wasm build loads into the pinned DuckDB-Wasm and routes on the sample graph.
// Only tie-insensitive facts are asserted (row count, path_seq, endpoints, total cost), because an
// equal-cost tie may legitimately resolve to another route.

import { readFileSync } from 'node:fs';
import { expect, test } from '@playwright/test';
import { bundledPgroutingVersion, extensionSource } from '../modes.mjs';

const PGROUTING_CMAKELISTS = new URL('../../../third_party/pgrouting/CMakeLists.txt', import.meta.url);

test('pgrouting loads and routes in DuckDB-Wasm', async ({ page }) => {
  const source = extensionSource(process.env);
  await page.goto('/');
  await page.waitForFunction(() => typeof window.runW2 === 'function');
  const result = await page.evaluate((s) => window.runW2(s), source);
  test.info().annotations.push(
    { type: 'bundle', description: String(result.variant) },
    { type: 'duckdb', description: String(result.duckdbVersion) },
    { type: 'extension', description: String(result.extensionUrl ?? source.mode) },
  );

  expect(result.error, result.error).toBeUndefined();
  expect(result.pgrVersion).toBe(bundledPgroutingVersion(readFileSync(PGROUTING_CMAKELISTS, 'utf8')));
  if (process.env.W2_EXPECT_EXTENSION_VERSION) {
    expect(result.extensionVersion).toBe(process.env.W2_EXPECT_EXTENSION_VERSION);
  }
  expect(result.dijkstra.map((r) => r.path_seq)).toEqual([1, 2, 3, 4, 5, 6]);
  expect(result.dijkstra[0]).toMatchObject({ node: 6, agg_cost: 0 });
  expect(result.dijkstra.at(-1)).toMatchObject({ node: 10, edge: -1, agg_cost: 5 });
  expect(result.dijkstraCost).toEqual([{ start_vid: 6, end_vid: 10, agg_cost: 5 }]);
});
