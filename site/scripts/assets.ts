// SPDX-License-Identifier: MIT
// Release asset names are pgrouting.<duckdb version>.<platform or wasm variant>.duckdb_extension.<gz|wasm>.
// The version itself contains dots, so the name is matched as a whole, never split on '.'.

const ASSET_NAME = /^pgrouting\.(v\d+\.\d+\.\d+)\.([a-z0-9_]+)\.duckdb_extension\.(gz|wasm)$/;

export interface WasmAsset {
  tag: string;
  publishedAt: string;
  name: string;
  duckdbVersion: string;
  variant: string;
}

export function parseAssetName(name: string): { duckdbVersion: string; target: string; kind: 'gz' | 'wasm' } | null {
  const match = ASSET_NAME.exec(name);
  if (!match) return null;
  const [, duckdbVersion = '', target = '', kind] = match;
  return { duckdbVersion, target, kind: kind === 'gz' ? 'gz' : 'wasm' };
}

// For every (DuckDB version, variant), the asset of the most recently published release.
export function newestPerTarget(assets: WasmAsset[]): WasmAsset[] {
  const newest = new Map<string, WasmAsset>();
  for (const a of assets) {
    const key = `${a.duckdbVersion}/${a.variant}`;
    const current = newest.get(key);
    if (!current || a.publishedAt > current.publishedAt) newest.set(key, a);
  }
  return [...newest.values()].sort(
    (a, b) => a.duckdbVersion.localeCompare(b.duckdbVersion) || a.variant.localeCompare(b.variant),
  );
}
