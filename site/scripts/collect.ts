// SPDX-License-Identifier: MIT
// Fills public/ before `vite dev` or `vite build`:
//   public/data/…      — every dataset under datasets/<id>/ (its dataset.json and presets.json) with
//                        the data files it names from test/data/<id>/, and data/index.json
//   public/wasm/…      — the Wasm builds of every published Release (newest per DuckDB version and
//                        variant), which the browser cannot fetch from GitHub itself (no CORS).
// Needs the gh CLI, authenticated (GH_TOKEN in CI). --allow-empty tolerates finding no Release.
import { execFileSync } from 'node:child_process';
import {
  copyFileSync,
  existsSync,
  mkdirSync,
  mkdtempSync,
  readdirSync,
  readFileSync,
  rmSync,
  writeFileSync,
} from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { buildIndex, parseDataset } from '../src/datasets.ts';
import { extensionPath } from '../src/paths.ts';
import { parsePresetFile } from '../src/presets.ts';
import { newestPerTarget, parseAssetName, type WasmAsset } from './assets.ts';

const site = join(dirname(fileURLToPath(import.meta.url)), '..');
const repo = process.env.GITHUB_REPOSITORY ?? 'sanak/duckdb-pgrouting';
const allowEmpty = process.argv.includes('--allow-empty');

function gh(args: string[]): string {
  return execFileSync('gh', args, { encoding: 'utf8', stdio: ['ignore', 'pipe', 'inherit'] });
}

function copyDatasets(): void {
  const from = join(site, 'datasets');
  const data = join(site, '..', 'test', 'data');
  const to = join(site, 'public', 'data');
  rmSync(to, { recursive: true, force: true });
  const entries: { id: string; title: string }[] = [];
  for (const id of readdirSync(from).sort()) {
    if (!existsSync(join(from, id, 'dataset.json'))) continue;
    // Parsing here fails the build on a broken file instead of shipping it.
    const dataset = parseDataset(id, JSON.parse(readFileSync(join(from, id, 'dataset.json'), 'utf8')));
    parsePresetFile(`${id}/presets.json`, JSON.parse(readFileSync(join(from, id, 'presets.json'), 'utf8')));
    mkdirSync(join(to, id), { recursive: true });
    for (const f of ['dataset.json', 'presets.json']) copyFileSync(join(from, id, f), join(to, id, f));
    for (const f of dataset.files) copyFileSync(join(data, id, f), join(to, id, f));
    if (existsSync(join(data, id, 'NOTICE.md'))) copyFileSync(join(data, id, 'NOTICE.md'), join(to, id, 'NOTICE.md'));
    entries.push({ id, title: dataset.title });
    console.log(`data: ${id} (${dataset.files.length} files)`);
  }
  writeFileSync(join(to, 'index.json'), `${JSON.stringify(buildIndex(entries), null, 2)}\n`);
}

function listWasmAssets(): WasmAsset[] {
  const releases = JSON.parse(
    gh(['release', 'list', '-R', repo, '--exclude-drafts', '--limit', '100', '--json', 'tagName,publishedAt']),
  ) as { tagName: string; publishedAt: string }[];
  const assets: WasmAsset[] = [];
  for (const r of releases) {
    const view = JSON.parse(gh(['release', 'view', r.tagName, '-R', repo, '--json', 'assets'])) as {
      assets: { name: string }[];
    };
    for (const { name } of view.assets) {
      const parsed = parseAssetName(name);
      if (parsed?.kind !== 'wasm') continue;
      assets.push({
        tag: r.tagName,
        publishedAt: r.publishedAt,
        name,
        duckdbVersion: parsed.duckdbVersion,
        variant: parsed.target,
      });
    }
  }
  return assets;
}

function copyWasmBuilds(): void {
  const to = join(site, 'public', 'wasm');
  rmSync(to, { recursive: true, force: true });
  let chosen: WasmAsset[];
  try {
    chosen = newestPerTarget(listWasmAssets());
  } catch (error) {
    if (!allowEmpty) throw error;
    console.warn(`wasm: could not list Releases (${String(error)}); continuing without Wasm builds`);
    return;
  }
  if (chosen.length === 0) {
    if (!allowEmpty) throw new Error('no published Release has a Wasm asset');
    console.warn('wasm: no published Release has a Wasm asset; continuing without Wasm builds');
    return;
  }
  const download = mkdtempSync(join(tmpdir(), 'pgrouting-wasm-'));
  try {
    for (const a of chosen) {
      gh(['release', 'download', a.tag, '-R', repo, '-p', a.name, '-D', download, '--clobber']);
      const target = join(site, 'public', extensionPath(a.duckdbVersion, a.variant));
      mkdirSync(dirname(target), { recursive: true });
      copyFileSync(join(download, a.name), target);
      console.log(`wasm: ${a.duckdbVersion}/${a.variant} from ${a.tag}`);
    }
  } finally {
    rmSync(download, { recursive: true, force: true });
  }
}

copyDatasets();
copyWasmBuilds();
