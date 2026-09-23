// SPDX-License-Identifier: GPL-2.0-or-later
// Where W2 loads the extension from. artifact and release serve a downloaded file from this test's
// own server, because a browser cannot fetch GitHub's Release downloads (no CORS headers); site
// loads from the deployed documentation site; community installs from DuckDB's community
// repository. {version} and {variant} are filled in by the page once DuckDB-Wasm has started.

export const DEFAULT_SITE_URL = 'https://sanak.github.io/duckdb-pgrouting';
export const LOCAL_TEMPLATE = '/ext/{variant}/pgrouting.duckdb_extension.wasm';

export function extensionSource(env) {
  const mode = env.W2_MODE || 'artifact';
  switch (mode) {
    case 'artifact':
    case 'release':
      if (!env.W2_EXTENSION_DIR) {
        throw new Error(`W2_MODE=${mode} needs W2_EXTENSION_DIR (see prepare-extension.mjs)`);
      }
      return { mode, urlTemplate: LOCAL_TEMPLATE };
    case 'site': {
      const base = (env.W2_SITE_URL || DEFAULT_SITE_URL).replace(/\/+$/, '');
      return { mode, urlTemplate: `${base}/wasm/{version}/{variant}/pgrouting.duckdb_extension.wasm` };
    }
    case 'community':
      return { mode, community: true };
    default:
      throw new Error(`unknown W2_MODE '${mode}' (artifact, release, site, community)`);
  }
}

export function bundledPgroutingVersion(cmakeText) {
  const match = /^project\(PGROUTING VERSION (\d+\.\d+\.\d+)/m.exec(cmakeText);
  if (!match) throw new Error("no 'project(PGROUTING VERSION x.y.z' line");
  return match[1];
}
