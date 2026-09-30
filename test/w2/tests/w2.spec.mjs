// SPDX-License-Identifier: GPL-2.0-or-later
// W2: the extension's Wasm build loads into the pinned DuckDB-Wasm and routes on the sample graph.
// Only facts the input order cannot change are asserted (endpoints, total costs, reached vertices,
// hop depths, spanning-forest size and weight, sets, counts, a clash-free coloring, an ordering
// that is a permutation), because an equal-cost tie or the order the edges arrive in may
// legitimately change the rows. One query per function family checks that its driver and result
// shape work in the browser; the one-value functions are called as scalar macros. The geometry
// functions run with duckdb-spatial, which the page loads from DuckDB's extension repository: the
// Wasm unittest skips every spatial test, so this is the only place they run in a browser before
// a release.

import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { expect, test } from '@playwright/test';
import { readFooter } from '../metadata.mjs';
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
  // The list reporter does not print annotations; this line shows the same facts in a CI log.
  console.log(`W2 bundle=${result.variant} duckdb=${result.duckdbVersion} reported=${JSON.stringify(result.extensionVersion)}`);

  expect(result.error, result.error).toBeUndefined();
  expect(result.pgrVersion).toBe(bundledPgroutingVersion(readFileSync(PGROUTING_CMAKELISTS, 'utf8')));
  // A locally served build is checked through its own metadata: DuckDB-Wasm reports an empty
  // extension_version for an extension LOADed by URL (see metadata.mjs).
  if (process.env.W2_EXTENSION_DIR) {
    const file = join(process.env.W2_EXTENSION_DIR, result.variant, 'pgrouting.duckdb_extension.wasm');
    const footer = readFooter(readFileSync(file));
    expect(footer.platform).toBe(result.variant);
    expect(footer.duckdbVersion).toBe(result.duckdbVersion);
    if (process.env.W2_EXPECT_EXTENSION_VERSION) {
      expect(footer.extensionVersion).toBe(process.env.W2_EXPECT_EXTENSION_VERSION);
    }
  }
  expect(result.dijkstra.map((r) => r.path_seq)).toEqual([1, 2, 3, 4, 5, 6]);
  expect(result.dijkstra[0]).toMatchObject({ node: 6, agg_cost: 0 });
  expect(result.dijkstra.at(-1)).toMatchObject({ node: 10, edge: -1, agg_cost: 5 });
  expect(result.dijkstraCost).toEqual([{ start_vid: 6, end_vid: 10, agg_cost: 5 }]);
  expect(result.components).toEqual([{ n: 17, components: 3 }]);
  expect(result.aStar.map((r) => r.path_seq)).toEqual([1, 2, 3, 4]);
  expect(result.aStar[0]).toMatchObject({ node: 6, agg_cost: 0 });
  expect(result.aStar.at(-1)).toMatchObject({ node: 12, edge: -1, agg_cost: 3 });
  expect(result.drivingDistance).toEqual(
    [[1, 3], [3, 2], [5, 3], [6, 2], [7, 1], [8, 2], [9, 3], [10, 3], [11, 0], [12, 1], [15, 2], [16, 1], [17, 2]].map(
      ([node, agg_cost]) => ({ node, agg_cost }),
    ),
  );
  expect(result.kruskal).toEqual([{ n: 14, total: 14 }]);
  expect(result.bfs).toEqual(
    [[1, 3], [3, 2], [5, 1], [6, 0], [7, 1], [8, 2], [9, 3], [10, 5], [11, 2], [12, 3], [15, 4], [16, 3], [17, 4]].map(
      ([node, depth]) => ({ node, depth }),
    ),
  );
  expect(result.dfs.map((r) => r.node)).toEqual([1, 3, 5, 6, 7, 8, 9, 10, 11, 12, 15, 16, 17]);
  expect(result.ksp).toEqual([
    { path_id: 1, cost: 4 },
    { path_id: 2, cost: 4 },
  ]);
  expect(result.via).toEqual([{ legs: 4, total: 11 }]);
  // 103, not the unrestricted 3: the restriction on edges 7 then 10 adds 100.
  expect(result.trsp[0]).toMatchObject({ node: 1, agg_cost: 0 });
  expect(result.trsp.at(-1)).toMatchObject({ node: 8, edge: -1, agg_cost: 103 });
  expect(result.tsp).toHaveLength(5);
  expect(result.tsp[0].node).toBe(5);
  expect(result.tsp.at(-1).node).toBe(5);
  expect([...new Set(result.tsp.map((r) => r.node))].sort((a, b) => a - b)).toEqual([5, 6, 10, 15]);
  expect(result.articulationPoints).toEqual([{ nodes: '3,6,7,8' }]);
  expect(result.coloring).toEqual([{ n: 17, clashes: 0 }]);
  expect(result.cuthillMckee).toEqual([{ n: 17, nodes: 17, first: 1, last: 17 }]);
  expect(result.floydWarshall).toEqual([{ n: 160, total: 462 }]);
  expect(result.betweenness).toHaveLength(1);
  expect(result.betweenness[0]).toMatchObject({ n: 17, top: 7 });
  expect(result.betweenness[0].centrality).toBeCloseTo(0.3416666666666667, 12);
  expect(result.isPlanar).toEqual([{ planar: true }]);
  expect(result.lineGraph).toEqual([{ n: 40 }]);
  expect(result.transitiveClosure).toEqual([
    {
      closure:
        '6:[] 8:[12, 16, 17] 10:[6, 11, 12, 16, 17] 11:[12, 16, 17] 12:[16, 17] 15:[6, 10, 11, 12, 16, 17] 16:[16, 17] 17:[16, 17]',
    },
  ]);
  // idom is the seq of the dominator's row, so the pairs are read through seq.
  expect(result.dominator).toEqual([
    { pairs: '1>3,2>0,3>7,4>0,5>0,6>5,7>6,8>7,9>8,10>15,11>7,12>7,13>0,14>0,15>16,16>7,17>7' },
  ]);
  expect(result.hawick).toEqual([{ n: 94, circuits: 20, total: 74 }]);
  expect(result.stoerWagner).toEqual([{ mincut: 1 }]);
  expect(result.maxFlow).toEqual([{ flow: 230 }]);
  expect(result.pushRelabel).toEqual([{ net: 230 }]);
  expect(result.chinesePostmanCost).toEqual([{ cost: 34 }]);
  // Which vertex a contracted tree collapses onto depends on the input order; the counts do not.
  expect(result.contraction).toEqual([
    { type: 'e', n: 4, absorbed: 6 },
    { type: 'v', n: 3, absorbed: 4 },
  ]);
  expect(result.contractionHierarchies).toEqual([{ ranked: 17 }]);
  expect(result.degree).toEqual([
    { degrees: '1:1,2:1,3:2,4:1,5:1,6:3,7:4,8:3,9:1,10:3,11:4,12:3,13:1,14:1,15:2,16:3,17:2' },
  ]);
  expect(result.vertices).toEqual([{ n: 17 }]);
  expect(result.closeEdges).toHaveLength(1);
  expect(result.closeEdges[0]).toMatchObject({ edge_id: 5, side: 'l' });
  expect(result.closeEdges[0].fraction).toBeCloseTo(0.8, 9);
});
