// SPDX-License-Identifier: GPL-2.0-or-later
// Lays downloaded Wasm builds out as <out>/<variant>/pgrouting.duckdb_extension.wasm, which
// server.mjs serves under /ext/. Accepts both what `gh run download` writes (one directory per CI
// artifact) and what `gh release download` writes (Release assets). The file name must stay
// pgrouting.duckdb_extension.wasm: DuckDB names an extension after its file name up to the first '.'.

import { copyFileSync, mkdirSync, readdirSync } from 'node:fs';
import { join, relative } from 'node:path';
import { pathToFileURL } from 'node:url';

const RELEASE_ASSET = /^pgrouting\.v\d+\.\d+\.\d+\.(wasm_[a-z]+)\.duckdb_extension\.wasm$/;
const ARTIFACT_DIR = /^pgrouting-v\d+\.\d+\.\d+-extension-(wasm_[a-z]+)$/;

export function variantOf(relativePath) {
  const parts = relativePath.split('/');
  const name = parts.at(-1);
  const asset = RELEASE_ASSET.exec(name);
  if (asset) return asset[1];
  if (name === 'pgrouting.duckdb_extension.wasm' && parts.length >= 2) {
    const artifact = ARTIFACT_DIR.exec(parts.at(-2));
    if (artifact) return artifact[1];
  }
  return null;
}

export function layout(relativePaths) {
  const plan = new Map();
  for (const path of relativePaths) {
    const variant = variantOf(path);
    if (!variant) continue;
    if (plan.has(variant)) throw new Error(`two files for ${variant}: ${plan.get(variant)}, ${path}`);
    plan.set(variant, path);
  }
  return plan;
}

function walk(root, dir = root) {
  return readdirSync(dir, { withFileTypes: true }).flatMap((entry) => {
    const full = join(dir, entry.name);
    return entry.isDirectory() ? walk(root, full) : [relative(root, full).split('\\').join('/')];
  });
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const [source, out] = process.argv.slice(2);
  if (!source || !out) throw new Error('usage: node prepare-extension.mjs <download dir> <out dir>');
  const plan = layout(walk(source));
  if (plan.size === 0) throw new Error(`no Wasm build found under ${source}`);
  for (const [variant, path] of plan) {
    mkdirSync(join(out, variant), { recursive: true });
    copyFileSync(join(source, path), join(out, variant, 'pgrouting.duckdb_extension.wasm'));
    console.log(`${variant}: ${path}`);
  }
}
