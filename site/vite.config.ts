// SPDX-License-Identifier: MIT
import { defineConfig } from 'vite';

export default defineConfig({
  // Served from https://sanak.github.io/duckdb-pgrouting/.
  base: '/duckdb-pgrouting/',
  // MapLibre's worker imports a shared chunk, so it must stay an ES module worker.
  worker: { format: 'es' },
  build: {
    // Licences of the bundled npm packages, linked from the page footer.
    license: { fileName: 'third-party-licenses.md' },
    // DuckDB-Wasm and MapLibre are large on their own; the warning says nothing actionable here.
    chunkSizeWarningLimit: 2000,
  },
});
