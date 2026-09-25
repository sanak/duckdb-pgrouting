// SPDX-License-Identifier: MIT
// Where the site serves each Wasm build of the extension, relative to its base URL. DuckDB names a
// loaded extension after its file name up to the first dot, so the file is always
// pgrouting.duckdb_extension.wasm; the DuckDB version and variant are directories.

export function extensionPath(duckdbVersion: string, variant: string): string {
  return `wasm/${duckdbVersion}/${variant}/pgrouting.duckdb_extension.wasm`;
}
