// SPDX-License-Identifier: MIT
// A dataset's network on MapLibre: the grey network, two result layers whose filter and colour
// follow each query's edge and node ids, an outline under the selected edge, and a wide invisible
// layer that makes the result's edges easy to click. The abstract sample graph is drawn on a blank
// background with a label per node; a geographic network is drawn over OpenFreeMap's basemap, with
// its nodes shown only when zoomed in (the workshop network has 22,889).
// A network the reader's own queries build (dataset.json dependsOn) may be empty at first: the map
// then opens on the dataset's view, is not held to the network's bounds, and takes each rebuilt
// network in place (setNetwork) without moving. Map inputs (dataset.json inputs) are buttons at the
// top left: an extent takes the current view; a point button starts a pick, and the next map click
// is the point. The map only reports values: the page writes them into the SQL (inputs.ts).
import {
  type ErrorEvent,
  type GeoJSONSource,
  type IControl,
  type LayerSpecification,
  LngLatBounds,
  Map as MapLibreMap,
  Marker,
  setWorkerUrl,
} from 'maplibre-gl';
import 'maplibre-gl/dist/maplibre-gl.css';
import mapWorkerUrl from 'maplibre-gl/dist/maplibre-gl-worker.mjs?worker&url';
import type { MapInput, View } from './datasets.ts';
import { idFilter, pathColour } from './expressions.ts';
import { type Bounds, boundsOverlap, extentOutline, type NetworkGeometry, viewBounds } from './geometry.ts';
import type { Box, InputValues, LonLat } from './inputs.ts';
import type { Highlight } from './result.ts';

// MapLibre locates its worker relative to its own module, which bundling breaks; Vite emits it.
setWorkerUrl(mapWorkerUrl);

const BASEMAP_STYLE = 'https://tiles.openfreemap.org/styles/positron';

// The padding a geographic map fits its view or network inside (the abstract map uses 48).
export const GEOGRAPHIC_PADDING = 24;

export interface MapOptions {
  mode: 'abstract' | 'geographic';
  // Shown in the attribution control for the network's own data (geographic mode).
  attribution?: string;
  // Where the map opens while the network is empty.
  view?: View;
  // The network is rebuilt by the reader's queries: the map is not held to its bounds.
  dynamic?: boolean;
  inputs?: MapInput[];
}

export interface RouteMap {
  highlight(h: Highlight): void;
  select(edge: number | null): void;
  // Called on every map click that is not a pick: with the result edge under the pointer, or null.
  onEdgeClick(handler: (edge: number | null) => void): void;
  // Replaces the drawn network and clears highlight and selection; the view does not move.
  setNetwork(geometry: NetworkGeometry): void;
  // Draws the inputs' values: the extent as a dashed outline, the points as lettered markers.
  showInputs(values: InputValues): void;
  // Called with an input and its new value: an extent on its button, a point on the click after it.
  onInput(handler: (input: MapInput, value: Box | LonLat) => void): void;
  setInputsEnabled(enabled: boolean): void;
  // What the map shows, as "Use this view" reads it, at the container's current size (the
  // resize observer has not caught up with a layout change yet); null for an abstract map.
  view(): Box | null;
  remove(): void;
}

function only(edge: number | null): Map<number, number> {
  return new Map(edge === null ? [] : [[edge, 0]]);
}

// The first point input is the start, the second the goal.
const POINT_MARKS = [
  { letter: 'S', className: 'mark-start' },
  { letter: 'G', className: 'mark-goal' },
];

function pointMark(inputs: readonly MapInput[], input: MapInput): { letter: string; className: string } {
  const index = inputs.filter((i) => i.kind === 'point').indexOf(input);
  return POINT_MARKS[index] ?? { letter: String(index + 1), className: 'mark-other' };
}

function networkLayers(geographic: boolean): LayerSpecification[] {
  const empty = new Map<number, number>();
  return [
    {
      id: 'edges',
      type: 'line',
      source: 'edges',
      paint: { 'line-color': geographic ? '#8a8a8a' : '#b5b5b5', 'line-width': geographic ? 1.5 : 3 },
    },
    {
      id: 'selected-edge',
      type: 'line',
      source: 'edges',
      filter: idFilter(empty),
      layout: { 'line-cap': 'round' },
      paint: { 'line-color': '#1f2328', 'line-width': geographic ? 10 : 13 },
    },
    {
      id: 'route-edges',
      type: 'line',
      source: 'edges',
      filter: idFilter(empty),
      layout: { 'line-cap': 'round' },
      paint: { 'line-color': pathColour(empty), 'line-width': geographic ? 5 : 7, 'line-opacity': 0.85 },
    },
    {
      id: 'nodes',
      type: 'circle',
      source: 'nodes',
      ...(geographic ? { minzoom: 16 } : {}),
      paint: {
        'circle-radius': ['match', ['get', 'kind'], 'point', 4, geographic ? 3 : 5],
        'circle-color': ['match', ['get', 'kind'], 'point', '#ffffff', '#6b6b6b'],
        'circle-stroke-color': '#6b6b6b',
        'circle-stroke-width': geographic ? 1 : 1.5,
      },
    },
    {
      id: 'route-nodes',
      type: 'circle',
      source: 'nodes',
      filter: idFilter(empty),
      paint: {
        'circle-radius': geographic ? 5 : 7,
        'circle-color': pathColour(empty),
        'circle-stroke-color': '#ffffff',
        'circle-stroke-width': 2,
      },
    },
    {
      id: 'edge-hit',
      type: 'line',
      source: 'edges',
      filter: idFilter(empty),
      paint: { 'line-color': '#000000', 'line-opacity': 0, 'line-width': 16 },
    },
  ];
}

// The input buttons, stacked in one MapLibre control group.
class InputControl implements IControl {
  readonly container = document.createElement('div');

  constructor(buttons: HTMLButtonElement[]) {
    this.container.className = 'maplibregl-ctrl maplibregl-ctrl-group map-inputs';
    this.container.append(...buttons);
  }

  onAdd(): HTMLElement {
    return this.container;
  }

  onRemove(): void {
    this.container.remove();
  }
}

export async function createRouteMap(
  container: HTMLElement,
  geometry: NetworkGeometry,
  options: MapOptions,
): Promise<RouteMap> {
  const geographic = options.mode === 'geographic';
  const inputs = options.inputs ?? [];
  const box = geometry.bounds ?? (options.view ? viewBounds(options.view) : null);
  if (!box) throw new Error('the map has nothing to draw');
  const [[west, south], [east, north]] = box;
  const padX = (east - west) * 0.5;
  const padY = (north - south) * 0.5;
  const edges = { type: 'geojson' as const, data: geometry.edges, attribution: options.attribution };
  const nodes = { type: 'geojson' as const, data: geometry.nodes };

  const map = new MapLibreMap({
    container,
    style: geographic
      ? BASEMAP_STYLE
      : {
          version: 8,
          sources: { edges, nodes },
          layers: [
            { id: 'background', type: 'background', paint: { 'background-color': '#f7f7f4' } },
            ...networkLayers(false),
          ],
        },
    bounds: new LngLatBounds(box[0], box[1]),
    fitBoundsOptions: { padding: geographic ? GEOGRAPHIC_PADDING : 48 },
    ...(options.dynamic
      ? {}
      : {
          maxBounds: [
            [west - padX, south - padY],
            [east + padX, north + padY],
          ] as [[number, number], [number, number]],
        }),
    renderWorldCopies: false,
    dragRotate: false,
    pitchWithRotate: false,
    touchPitch: false,
    attributionControl: geographic ? { compact: true } : false,
  });
  map.touchZoomRotate.disableRotation();

  if (!geographic) {
    // Labels as HTML markers, so the style needs no glyphs endpoint.
    for (const f of geometry.nodes.features) {
      const label = document.createElement('div');
      label.className = `node-label ${f.properties.kind}`;
      label.textContent = f.properties.label;
      const [lng = 0, lat = 0] = f.geometry.coordinates;
      new Marker({ element: label, anchor: 'bottom-left', offset: [5, -3] }).setLngLat([lng, lat]).addTo(map);
    }
  }

  if (geographic) {
    // OpenFreeMap is a third party: offline or blocked, the style never loads and 'load' never
    // fires. Race it against the first error seen before load, which fires instead; once loaded,
    // a later tile error is no longer this function's problem.
    await new Promise<void>((resolve, reject) => {
      const onError = (event: ErrorEvent) => {
        map.off('error', onError);
        map.remove();
        reject(new Error(`the basemap could not be loaded: ${event.error.message}`));
      };
      map.on('error', onError);
      map.once('load').then(() => {
        map.off('error', onError);
        resolve();
      });
    });
    map.addSource('edges', edges);
    map.addSource('nodes', nodes);
    for (const layer of networkLayers(true)) map.addLayer(layer);
  } else {
    await map.once('load');
  }

  let edgeHandler: ((edge: number | null) => void) | null = null;
  let inputHandler: ((input: MapInput, value: Box | LonLat) => void) | null = null;
  // The point input whose button was pressed and whose map click has not come yet.
  let picking: MapInput | null = null;
  const buttons = new Map<MapInput, HTMLButtonElement>();
  const markers = new Map<string, Marker>();
  const hint = document.createElement('div');
  hint.className = 'map-hint';
  hint.hidden = true;
  map.getContainer().append(hint);

  function onKey(event: KeyboardEvent): void {
    if (event.key === 'Escape') stopPicking();
  }

  function shownBox(): Box {
    const b = map.getBounds();
    return { xmin: b.getWest(), ymin: b.getSouth(), xmax: b.getEast(), ymax: b.getNorth() };
  }

  function stopPicking(): void {
    if (!picking) return;
    buttons.get(picking)?.setAttribute('aria-pressed', 'false');
    picking = null;
    hint.hidden = true;
    map.getCanvas().style.cursor = '';
    document.removeEventListener('keydown', onKey);
  }

  function startPicking(input: MapInput): void {
    stopPicking();
    picking = input;
    buttons.get(input)?.setAttribute('aria-pressed', 'true');
    hint.textContent = `Click the map to set “${input.label}” · Esc cancels`;
    hint.hidden = false;
    map.getCanvas().style.cursor = 'crosshair';
    document.addEventListener('keydown', onKey);
  }

  for (const input of inputs) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'map-input';
    const icon = document.createElement('span');
    if (input.kind === 'extent') {
      icon.className = 'input-icon extent';
    } else {
      const mark = pointMark(inputs, input);
      icon.className = `input-icon mark ${mark.className}`;
      icon.textContent = mark.letter;
      button.setAttribute('aria-pressed', 'false');
    }
    button.append(icon, input.label);
    button.addEventListener('click', () => {
      if (input.kind === 'extent') {
        stopPicking();
        inputHandler?.(input, shownBox());
      } else if (picking === input) {
        stopPicking();
      } else {
        startPicking(input);
      }
    });
    buttons.set(input, button);
  }
  if (buttons.size > 0) map.addControl(new InputControl([...buttons.values()]), 'top-left');
  if (inputs.some((input) => input.kind === 'extent')) {
    map.addSource('input-extent', { type: 'geojson', data: extentOutline(null) });
    map.addLayer({
      id: 'input-extent',
      type: 'line',
      source: 'input-extent',
      paint: { 'line-color': '#1f2328', 'line-width': 1.5, 'line-dasharray': [3, 2] },
    });
  }

  map.on('mouseenter', 'edge-hit', () => {
    if (!picking) map.getCanvas().style.cursor = 'pointer';
  });
  map.on('mouseleave', 'edge-hit', () => {
    if (!picking) map.getCanvas().style.cursor = '';
  });
  map.on('click', (event) => {
    if (picking) {
      const input = picking;
      stopPicking();
      inputHandler?.(input, [event.lngLat.lng, event.lngLat.lat]);
      return;
    }
    const id = map.queryRenderedFeatures(event.point, { layers: ['edge-hit'] })[0]?.properties.id;
    edgeHandler?.(typeof id === 'number' ? id : null);
  });

  function highlight(h: Highlight): void {
    map.setFilter('route-edges', idFilter(h.edges));
    map.setPaintProperty('route-edges', 'line-color', pathColour(h.edges));
    map.setFilter('route-nodes', idFilter(h.nodes));
    map.setPaintProperty('route-nodes', 'circle-color', pathColour(h.nodes));
    map.setFilter('edge-hit', idFilter(h.edges));
  }

  function select(edge: number | null): void {
    map.setFilter('selected-edge', idFilter(only(edge)));
  }

  return {
    highlight,
    select,
    onEdgeClick(handler: (edge: number | null) => void) {
      edgeHandler = handler;
    },
    setNetwork(next: NetworkGeometry) {
      map.getSource<GeoJSONSource>('edges')?.setData(next.edges);
      map.getSource<GeoJSONSource>('nodes')?.setData(next.nodes);
      // The view stays put unless the new network lies wholly outside it (an area typed by hand).
      const shown = map.getBounds().toArray() as Bounds;
      if (next.bounds && !boundsOverlap(next.bounds, shown))
        map.fitBounds(next.bounds, { padding: GEOGRAPHIC_PADDING });
      highlight({ edges: new Map(), nodes: new Map() });
      select(null);
    },
    showInputs(values: InputValues) {
      map.getSource<GeoJSONSource>('input-extent')?.setData(extentOutline(values.extent ?? null));
      for (const input of inputs) {
        if (input.kind !== 'point') continue;
        const at = values.points.get(input.variable);
        const marker = markers.get(input.variable);
        if (!at) {
          marker?.remove();
          markers.delete(input.variable);
        } else if (marker) {
          marker.setLngLat(at);
        } else {
          const mark = pointMark(inputs, input);
          const element = document.createElement('div');
          element.className = `input-mark ${mark.className}`;
          element.textContent = mark.letter;
          element.title = input.label;
          markers.set(input.variable, new Marker({ element }).setLngLat(at).addTo(map));
        }
      }
    },
    onInput(handler: (input: MapInput, value: Box | LonLat) => void) {
      inputHandler = handler;
    },
    setInputsEnabled(enabled: boolean) {
      if (!enabled) stopPicking();
      for (const button of buttons.values()) button.disabled = !enabled;
    },
    view() {
      if (!geographic) return null;
      map.resize();
      return shownBox();
    },
    remove() {
      stopPicking();
      map.remove();
    },
  };
}
