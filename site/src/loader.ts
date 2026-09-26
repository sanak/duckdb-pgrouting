// SPDX-License-Identifier: MIT
// Fetches a dataset's files from this site and builds it, once, in its own catalog. Opening a
// dataset again only selects its catalog, so what a reader created there (views, macros, the
// workshop's vertices table) is still there. A build that failed part-way is retried on the next
// open: setupStatements() can run twice. Opens are serialized on the one connection: a build and
// the USE that follows it depend on whichever catalog is currently selected, so two opens must
// never interleave. A failed open may leave the half-built catalog selected; the caller is
// expected to re-open the previous dataset. network() builds each dataset's map geometry once,
// inside that same queue, so its unqualified queries never run against a catalog a later queued
// open has since selected.
import {
  type Dataset,
  type DatasetIndex,
  type MapSpec,
  parseDataset,
  parseDatasetIndex,
  setupStatements,
  useStatement,
} from './datasets.ts';
import type { Session } from './duckdb.ts';
import { geographicGeometry, type NetworkGeometry, sampleGeometry } from './geometry.ts';
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

export function createDatasetLoader(session: Session): {
  open(id: string): Promise<OpenDataset>;
  network(id: string): Promise<NetworkGeometry>;
} {
  const built = new Map<string, Promise<OpenDataset>>();
  const networks = new Map<string, Promise<NetworkGeometry>>();
  // Serializes every open() and network() on this connection: each queued turn's build-if-needed,
  // USE and (for network()) map queries only start once the previous turn has settled, whichever
  // way. The chain itself must never end up rejected, or every later turn would wait on a promise
  // that is already dead.
  let queue: Promise<unknown> = Promise.resolve();

  function enqueue<T>(task: () => Promise<T>): Promise<T> {
    const turn = queue.then(task);
    queue = turn.catch(() => {});
    return turn;
  }

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
      return enqueue(() => openNow(id));
    },
    network(id: string): Promise<NetworkGeometry> {
      let pending = networks.get(id);
      if (!pending) {
        pending = enqueue(async () => {
          const { dataset } = await openNow(id);
          return networkOf(session, dataset.map);
        });
        networks.set(id, pending);
        pending.catch(() => networks.delete(id));
      }
      return pending;
    },
  };
}

// The dataset's network for the map, from the queries in its dataset.json.
export async function networkOf(session: Session, spec: MapSpec): Promise<NetworkGeometry> {
  const numbers = async (sql: string) => (await session.query(sql)).rows.map((row) => row.map(Number));
  if (spec.mode === 'abstract') {
    const [vertices, edges, points] = await Promise.all([
      numbers(spec.vertices),
      numbers(spec.edges),
      numbers(spec.points),
    ]);
    return sampleGeometry(
      vertices.map(([id = 0, x = 0, y = 0]) => ({ id, x, y })),
      edges.map(([id = 0, source = 0, target = 0]) => ({ id, source, target })),
      points.map(([pid = 0, edge_id = 0, fraction = 0]) => ({ pid, edge_id, fraction })),
    );
  }
  const edges = (await session.query(spec.edges)).rows.map(([id, geojson]) => ({
    id: Number(id),
    geojson: String(geojson),
  }));
  const nodes = (await numbers(spec.nodes)).map(([id = 0, x = 0, y = 0]) => ({ id, x, y }));
  return geographicGeometry(edges, nodes);
}
