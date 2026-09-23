// SPDX-License-Identifier: GPL-2.0-or-later
// Static server for the W2 page, standard library only. It serves the pinned DuckDB-Wasm package
// from node_modules rather than a CDN, so the test exercises exactly the version package.json pins.

import { createReadStream, statSync } from 'node:fs';
import { createServer } from 'node:http';
import { dirname, extname, join, resolve, sep } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));

const TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.map': 'application/json',
  '.wasm': 'application/wasm',
  '.csv': 'text/csv; charset=utf-8',
};

export function resolvePath(urlPath, roots) {
  let path;
  try {
    path = decodeURIComponent(urlPath);
  } catch {
    return null;
  }
  if (Object.hasOwn(roots.files, path)) return roots.files[path];
  for (const [prefix, dir] of roots.prefixes) {
    if (!dir || !path.startsWith(prefix)) continue;
    const root = resolve(dir);
    const target = resolve(root, `.${sep}${path.slice(prefix.length)}`);
    return target.startsWith(root + sep) ? target : null;
  }
  return null;
}

function defaultRoots() {
  return {
    files: { '/': join(HERE, 'index.html'), '/harness.js': join(HERE, 'build/harness.js') },
    prefixes: [
      ['/duckdb/', join(HERE, 'node_modules/@duckdb/duckdb-wasm/dist')],
      ['/data/', resolve(HERE, '../data/pgrouting_sample')],
      ['/ext/', process.env.W2_EXTENSION_DIR || null],
    ],
  };
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const roots = defaultRoots();
  const port = Number(process.env.W2_PORT || 4173);
  createServer((request, response) => {
    const file = resolvePath(new URL(request.url, 'http://localhost').pathname, roots);
    let stat;
    try {
      stat = file && statSync(file);
    } catch {
      stat = null;
    }
    if (!stat || !stat.isFile()) {
      response.writeHead(404, { 'Content-Type': 'text/plain' });
      response.end('not found');
      return;
    }
    response.writeHead(200, {
      'Content-Type': TYPES[extname(file)] || 'application/octet-stream',
      'Content-Length': stat.size,
      'Cache-Control': 'no-store',
    });
    if (request.method === 'HEAD') {
      response.end();
      return;
    }
    createReadStream(file).pipe(response);
  }).listen(port, '127.0.0.1', () => console.log(`W2 server on http://127.0.0.1:${port}/`));
}
