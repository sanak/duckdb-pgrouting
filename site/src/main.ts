// SPDX-License-Identifier: MIT
import './style.css';
import { ExtensionLoadError, type Session, startSession } from './duckdb.ts';
import { sampleGeometry } from './geometry.ts';
import { createRouteMap, type RouteMap } from './map.ts';
import { PRESETS } from './presets.ts';
import { highlightOf, type ResultSet, summarize } from './result.ts';
import { nextSelection, type PickSource } from './selection.ts';
import { hashForQuery, queryFromHash } from './share.ts';
import { createResultGrid } from './table.ts';

const MAX_TABLE_ROWS = 1000;

function byId<T extends HTMLElement>(id: string): T {
  const el = document.getElementById(id);
  if (!el) throw new Error(`#${id} is missing`);
  return el as T;
}

const sql = byId<HTMLTextAreaElement>('sql');
const preset = byId<HTMLSelectElement>('preset');
const run = byId<HTMLButtonElement>('run');
const share = byId<HTMLButtonElement>('share');
const status = byId<HTMLParagraphElement>('status');
const banner = byId<HTMLDivElement>('banner');
const versions = byId<HTMLParagraphElement>('versions');
const grid = createResultGrid(byId<HTMLDivElement>('result'));

function setStatus(text: string, isError = false): void {
  status.textContent = text;
  status.classList.toggle('error', isError);
}

for (const p of PRESETS) preset.append(new Option(p.label, p.id));
preset.addEventListener('change', () => {
  const chosen = PRESETS.find((p) => p.id === preset.value);
  if (chosen) sql.value = chosen.sql;
});
sql.value = queryFromHash(location.hash) || PRESETS[0]?.sql || '';

share.addEventListener('click', async () => {
  history.replaceState(null, '', hashForQuery(sql.value));
  try {
    await navigator.clipboard.writeText(location.href);
    setStatus('Link copied to the clipboard.');
  } catch {
    setStatus('The link is in the address bar.');
  }
});

function numbers(result: ResultSet): number[][] {
  return result.rows.map((row) => row.map(Number));
}

async function main(): Promise<void> {
  let session: Session;
  try {
    session = await startSession();
  } catch (error) {
    banner.textContent =
      error instanceof ExtensionLoadError
        ? `${error.message}. This site serves builds for the DuckDB versions of published releases only.`
        : `Could not start DuckDB: ${error instanceof Error ? error.message : String(error)}`;
    banner.hidden = false;
    versions.textContent = '';
    return;
  }
  versions.textContent = `pgRouting ${session.pgrVersion} · DuckDB ${session.duckdbVersion} · ${session.variant}`;

  // Without the map (no WebGL, for instance) queries still run; only the highlighting is lost.
  let routeMap: RouteMap = { highlight() {}, select() {}, onEdgeClick() {} };
  try {
    const vertices = numbers(await session.query('SELECT id, x, y FROM vertices'));
    const edges = numbers(await session.query('SELECT id, source, target FROM edges'));
    const points = numbers(await session.query('SELECT pid, edge_id, fraction FROM pointsofinterest'));
    routeMap = await createRouteMap(
      byId('map'),
      sampleGeometry(
        vertices.map(([id = 0, x = 0, y = 0]) => ({ id, x, y })),
        edges.map(([id = 0, source = 0, target = 0]) => ({ id, source, target })),
        points.map(([pid = 0, edge_id = 0, fraction = 0]) => ({ pid, edge_id, fraction })),
      ),
    );
  } catch (error) {
    banner.textContent = `The map is unavailable, but queries still run: ${error instanceof Error ? error.message : String(error)}`;
    banner.hidden = false;
  }

  // The one edge selected in both the grid and the map (edge ids only; see selection.ts).
  let selected: number | null = null;
  function pick(edge: number | null, source: PickSource): void {
    selected = nextSelection(selected, edge, source);
    grid.select(selected, { scroll: source === 'map' });
    routeMap.select(selected);
  }
  grid.onRowPick((edge) => pick(edge, 'grid'));
  routeMap.onEdgeClick((edge) => pick(edge, 'map'));

  async function execute(): Promise<void> {
    if (run.disabled) return;
    run.disabled = true;
    setStatus('Running…');
    const started = performance.now();
    try {
      const result = await session.query(sql.value);
      const { shown, text } = summarize(result, MAX_TABLE_ROWS);
      grid.show(result, shown);
      selected = null;
      routeMap.select(null);
      routeMap.highlight(highlightOf(result));
      setStatus(`${text} · ${Math.round(performance.now() - started)} ms`);
    } catch (error) {
      setStatus(error instanceof Error ? error.message : String(error), true);
    } finally {
      run.disabled = false;
    }
  }

  run.addEventListener('click', execute);
  sql.addEventListener('keydown', (event) => {
    if (event.key === 'Enter' && (event.ctrlKey || event.metaKey)) {
      event.preventDefault();
      void execute();
    }
  });
  run.disabled = false;
  setStatus('Ready. Ctrl+Enter (⌘+Enter) runs the query.');
}

void main();
