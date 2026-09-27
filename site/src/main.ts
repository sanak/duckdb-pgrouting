// SPDX-License-Identifier: MIT
import './style.css';
import { type Dataset, type MapInput, networkHint } from './datasets.ts';
import { ExtensionLoadError, type Session, startSession } from './duckdb.ts';
import { applyInput, type Box, type LonLat, readInputs } from './inputs.ts';
import { createDatasetLoader, fetchIndex, type OpenDataset } from './loader.ts';
import { createRouteMap, type RouteMap } from './map.ts';
import { createdNames, type Preset, type PresetFile, prerequisiteHint, presetGroups } from './presets.ts';
import { highlightOf, summarize } from './result.ts';
import { nextSelection, type PickSource } from './selection.ts';
import { hashFor, stateFromHash } from './share.ts';
import { createResultGrid } from './table.ts';

const MAX_TABLE_ROWS = 1000;
const READY_STATUS = 'Ready. Ctrl+Enter (⌘+Enter) runs the query.';
// Without the map (no WebGL, or its data failed) queries still run; only the highlighting is lost.
const NO_MAP: RouteMap = {
  highlight() {},
  select() {},
  onEdgeClick() {},
  setNetwork() {},
  showInputs() {},
  onInput() {},
  setInputsEnabled() {},
  remove() {},
};

function byId<T extends HTMLElement>(id: string): T {
  const el = document.getElementById(id);
  if (!el) throw new Error(`#${id} is missing`);
  return el as T;
}

const sql = byId<HTMLTextAreaElement>('sql');
const datasetSelect = byId<HTMLSelectElement>('dataset');
const preset = byId<HTMLSelectElement>('preset');
const presetNote = byId<HTMLParagraphElement>('preset-note');
const run = byId<HTMLButtonElement>('run');
const share = byId<HTMLButtonElement>('share');
const status = byId<HTMLParagraphElement>('status');
const banner = byId<HTMLDivElement>('banner');
const versions = byId<HTMLParagraphElement>('versions');
const mapSection = byId<HTMLElement>('map');
const grid = createResultGrid(byId<HTMLDivElement>('result'));

function messageOf(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

function mapInputs(dataset: Dataset): MapInput[] {
  return dataset.map.mode === 'geographic' ? (dataset.map.inputs ?? []) : [];
}

// The tables the map draws when the reader's queries build them (dataset.json dependsOn).
function dependsOn(dataset: Dataset): string[] {
  return dataset.map.mode === 'geographic' ? (dataset.map.dependsOn ?? []) : [];
}

function setStatus(text: string, isError = false): void {
  status.textContent = text;
  status.classList.toggle('error', isError);
}

function showBanner(text: string): void {
  banner.textContent = text;
  banner.hidden = text === '';
}

function setBusy(busy: boolean): void {
  run.disabled = busy;
  datasetSelect.disabled = busy;
  preset.disabled = busy;
}

function link(href: string, label: string): HTMLAnchorElement {
  const a = document.createElement('a');
  a.href = href;
  a.textContent = label;
  a.target = '_blank';
  a.rel = 'noopener';
  return a;
}

function showPresets(presets: readonly Preset[]): void {
  preset.replaceChildren();
  for (const { group, presets: members } of presetGroups(presets)) {
    const optgroup = document.createElement('optgroup');
    optgroup.label = group;
    for (const p of members) optgroup.append(new Option(p.label, p.id));
    preset.append(optgroup);
  }
}

// Attribution, chapter link and licence of the chosen preset, for files that carry an attribution.
function showPresetNote(file: PresetFile, chosen: Preset | undefined): void {
  presetNote.replaceChildren();
  presetNote.hidden = !(chosen && file.attribution);
  if (!chosen || !file.attribution) return;
  presetNote.append(`${file.attribution} `);
  if (chosen.source) presetNote.append(link(chosen.source, file.sourceLabel ?? 'Workshop page'), ' · ');
  presetNote.append(file.licenseUrl ? link(file.licenseUrl, file.license) : file.license);
}

async function main(): Promise<void> {
  let session: Session;
  let index: Awaited<ReturnType<typeof fetchIndex>>;
  try {
    [session, index] = await Promise.all([startSession(), fetchIndex()]);
  } catch (error) {
    showBanner(
      error instanceof ExtensionLoadError
        ? `${error.message}. This site serves builds for the DuckDB versions of published releases only.`
        : `Could not start DuckDB: ${messageOf(error)}`,
    );
    versions.textContent = '';
    return;
  }
  versions.textContent = `pgRouting ${session.pgrVersion} · DuckDB ${session.duckdbVersion} · ${session.variant}`;
  for (const d of index.datasets) datasetSelect.append(new Option(d.title, d.id));

  const loader = createDatasetLoader(session);
  let current: { id: string; opened: OpenDataset } | null = null;
  let routeMap = NO_MAP;
  // The one edge selected in both the grid and the map (edge ids only; see selection.ts).
  let selected: number | null = null;
  // True while a dependsOn dataset's network has not been built yet.
  let networkEmpty = false;

  function readyStatus(): string {
    const first = current?.opened.presets.presets[0];
    return networkEmpty && first
      ? `${READY_STATUS}\nNo network yet — run the presets from “${first.group}” onward.`
      : READY_STATUS;
  }

  // Busy like setBusy(), and the map's input buttons with it.
  function setAllBusy(busy: boolean): void {
    setBusy(busy);
    routeMap.setInputsEnabled(!busy);
  }

  function showInputValues(): void {
    if (current) routeMap.showInputs(readInputs(sql.value, mapInputs(current.opened.dataset)));
  }

  function onMapInput(input: MapInput, value: Box | LonLat): void {
    if (!current) return;
    const outcome = applyInput(sql.value, input, value, current.opened.presets.presets);
    if ('error' in outcome) {
      setStatus(outcome.error, true);
      return;
    }
    sql.value = outcome.sql;
    showInputValues();
    setStatus(`${input.label}: the SET VARIABLE ${input.variable} line now holds it. Run the query to use it.`);
  }

  function pick(edge: number | null, source: PickSource): void {
    selected = nextSelection(selected, edge, source);
    grid.select(selected, { scroll: source === 'map' });
    routeMap.select(selected);
  }
  grid.onRowPick((edge) => pick(edge, 'grid'));

  async function rebuildMap(opened: OpenDataset): Promise<void> {
    const old = routeMap;
    routeMap = NO_MAP;
    networkEmpty = false;
    try {
      old.remove();
      mapSection.replaceChildren();
      // Built once per dataset by the loader, inside its queue (see loader.ts).
      const geometry = await loader.network(opened.dataset.id);
      const map = opened.dataset.map;
      const geographic = map.mode === 'geographic' ? map : null;
      routeMap = await createRouteMap(mapSection, geometry, {
        mode: map.mode,
        attribution: geographic?.attribution,
        view: geographic?.view,
        dynamic: dependsOn(opened.dataset).length > 0,
        inputs: mapInputs(opened.dataset),
      });
      networkEmpty = dependsOn(opened.dataset).length > 0 && geometry.edges.features.length === 0;
      routeMap.onEdgeClick((edge) => pick(edge, 'map'));
      routeMap.onInput(onMapInput);
      routeMap.setInputsEnabled(!run.disabled);
      showInputValues();
    } catch (error) {
      showBanner(`The map is unavailable, but queries still run: ${messageOf(error)}`);
    }
  }

  // Nothing is open: Run stays disabled, and the blank selection makes choosing any dataset fire
  // `change`, including the one that just failed.
  function nothingOpen(): void {
    current = null;
    datasetSelect.value = '';
    setStatus('Choose a dataset to continue.');
  }

  // Opens a dataset (building it on first use) and shows its presets. `text` is SQL from a share
  // link; without it the editor gets the dataset's first preset. On failure the previous dataset
  // stays selected and usable; when there is none (the first open failed), the default dataset is
  // opened instead. `notice` is an earlier failure kept at the head of the banner.
  async function openDataset(id: string, text: string | null, notice = ''): Promise<boolean> {
    const previous = current;
    let failure: string | null = null;
    setAllBusy(true);
    showBanner(notice);
    const title = index.datasets.find((d) => d.id === id)?.title ?? id;
    setStatus(`Loading ${title}…`);
    try {
      const opened = await loader.open(id);
      current = { id, opened };
      datasetSelect.value = id;
      const presets = opened.presets.presets;
      showPresets(presets);
      const first = presets[0];
      if (text !== null) {
        sql.value = text;
        preset.value = '';
      } else if (first) {
        sql.value = first.sql;
        preset.value = first.id;
      }
      showPresetNote(
        opened.presets,
        presets.find((p) => p.id === preset.value),
      );
      grid.show({ columns: [], rows: [] }, []);
      selected = null;
      await rebuildMap(opened);
      setStatus(readyStatus());
      return true;
    } catch (error) {
      failure = `${notice ? `${notice} ` : ''}Could not load ${title}: ${messageOf(error)}`;
      showBanner(failure);
      setStatus('');
      if (previous) {
        // The failed build may have left its own catalog selected; select the previous one again.
        try {
          await loader.open(previous.id);
          current = previous;
          datasetSelect.value = previous.id;
          setStatus(readyStatus());
        } catch (reopenError) {
          showBanner(`${failure}. Returning to ${previous.opened.dataset.title} failed too: ${messageOf(reopenError)}`);
          nothingOpen();
        }
        failure = null;
      }
    } finally {
      setAllBusy(false);
      if (!current) run.disabled = true;
    }
    // Only a failed first open gets here with a failure: fall back to the default dataset, once.
    if (failure !== null) {
      if (id !== index.default) await openDataset(index.default, null, failure);
      else nothingOpen();
    }
    return false;
  }

  async function execute(): Promise<void> {
    if (run.disabled || !current) return;
    // Busy like a dataset load, so a load and a query never overlap on the one connection.
    setAllBusy(true);
    setStatus('Running…');
    const started = performance.now();
    try {
      const result = await session.query(sql.value);
      const { shown, text } = summarize(result, MAX_TABLE_ROWS);
      grid.show(result, shown);
      const built = dependsOn(current.opened.dataset);
      if (built.length > 0 && createdNames(sql.value).some((name) => built.includes(name))) {
        try {
          const geometry = await loader.refreshNetwork(current.id);
          networkEmpty = geometry.edges.features.length === 0;
          routeMap.setNetwork(geometry);
        } catch (error) {
          showBanner(`The map could not be redrawn: ${messageOf(error)}`);
        }
      }
      selected = null;
      routeMap.select(null);
      routeMap.highlight(highlightOf(result));
      setStatus(`${text} · ${Math.round(performance.now() - started)} ms`);
    } catch (error) {
      const message = messageOf(error);
      const hints = [
        prerequisiteHint(message, current.opened.presets.presets, preset.value || null),
        networkHint(message, current.opened.dataset),
      ].filter((hint): hint is string => hint !== null);
      setStatus([message, ...hints].join('\n'), true);
    } finally {
      setAllBusy(false);
      if (!current) run.disabled = true;
    }
  }

  preset.addEventListener('change', () => {
    if (!current) return;
    const chosen = current.opened.presets.presets.find((p) => p.id === preset.value);
    if (chosen) sql.value = chosen.sql;
    showPresetNote(current.opened.presets, chosen);
    showInputValues();
  });
  datasetSelect.addEventListener('change', () => {
    history.replaceState(null, '', location.pathname + location.search);
    void openDataset(datasetSelect.value, null);
  });
  share.addEventListener('click', async () => {
    if (!current) return;
    history.replaceState(null, '', hashFor(current.id, sql.value));
    try {
      await navigator.clipboard.writeText(location.href);
      setStatus('Link copied to the clipboard.');
    } catch {
      setStatus('The link is in the address bar.');
    }
  });
  run.addEventListener('click', execute);
  sql.addEventListener('keydown', (event) => {
    if (event.key === 'Enter' && (event.ctrlKey || event.metaKey)) {
      event.preventDefault();
      void execute();
    }
  });
  // The outline and markers follow the SET VARIABLE lines as the reader types.
  let inputTimer: ReturnType<typeof setTimeout> | undefined;
  sql.addEventListener('input', () => {
    clearTimeout(inputTimer);
    inputTimer = setTimeout(showInputValues, 200);
  });

  const linked = stateFromHash(location.hash);
  const known = linked.dataset !== null && index.datasets.some((d) => d.id === linked.dataset);
  const started = await openDataset(known && linked.dataset ? linked.dataset : index.default, linked.sql);
  if (started && linked.dataset !== null && !known) {
    setStatus(`This page has no dataset named “${linked.dataset}”; opened ${index.default} instead.`);
  }
}

void main();
