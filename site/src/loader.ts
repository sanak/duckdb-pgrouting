// SPDX-License-Identifier: MIT
// Fetches a dataset's files from this site and builds it, once, in its own catalog. Opening a
// dataset again only selects its catalog, so what a reader created there (views, macros, the
// workshop's vertices table) is still there. A build that failed part-way is retried on the next
// open: setupStatements() can run twice. Opens are serialized on the one connection: a build and
// the USE that follows it depend on whichever catalog is currently selected, so two opens must
// never interleave. A failed open may leave the half-built catalog selected; the caller is
// expected to re-open the previous dataset.
import {
  type Dataset,
  type DatasetIndex,
  parseDataset,
  parseDatasetIndex,
  setupStatements,
  useStatement,
} from './datasets.ts';
import type { Session } from './duckdb.ts';
import { type PresetFile, parsePresetFile } from './presets.ts';

export interface OpenDataset {
  dataset: Dataset;
  presets: PresetFile;
}

async function fetchData(path: string): Promise<Response> {
  const response = await fetch(`${import.meta.env.BASE_URL}data/${path}`);
  if (!response.ok) throw new Error(`Could not fetch data/${path}: HTTP ${response.status}`);
  return response;
}

export async function fetchIndex(): Promise<DatasetIndex> {
  return parseDatasetIndex(await (await fetchData('index.json')).json());
}

export function createDatasetLoader(session: Session): { open(id: string): Promise<OpenDataset> } {
  const built = new Map<string, Promise<OpenDataset>>();
  // Serializes every open() on this connection: each open's build-if-needed-then-USE only starts
  // once the previous open has settled, whichever way. The chain itself must never end up
  // rejected, or every later open would wait on a promise that is already dead.
  let queue: Promise<unknown> = Promise.resolve();

  async function build(id: string): Promise<OpenDataset> {
    const dataset = parseDataset(id, await (await fetchData(`${id}/dataset.json`)).json());
    const presets = parsePresetFile(`${id}/presets.json`, await (await fetchData(`${id}/presets.json`)).json());
    for (const file of dataset.files) {
      const bytes = new Uint8Array(await (await fetchData(`${id}/${file}`)).arrayBuffer());
      await session.registerFile(`${id}/${file}`, bytes);
    }
    for (const statement of setupStatements(dataset)) await session.query(statement);
    return { dataset, presets };
  }

  async function openNow(id: string): Promise<OpenDataset> {
    let pending = built.get(id);
    if (!pending) {
      pending = build(id);
      built.set(id, pending);
      pending.catch(() => built.delete(id));
    }
    const opened = await pending;
    await session.query(useStatement(id));
    return opened;
  }

  return {
    open(id: string): Promise<OpenDataset> {
      const turn = queue.then(() => openNow(id));
      queue = turn.catch(() => {});
      return turn;
    },
  };
}
