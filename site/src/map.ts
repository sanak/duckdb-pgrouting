// SPDX-License-Identifier: MIT
// A dataset's network on MapLibre: the grey network, two result layers whose filter and colour
// follow each query's edge and node ids, an outline under the selected edge, and a wide invisible
// layer that makes the result's edges easy to click. The abstract sample graph is drawn on a blank
// background with a label per node; a geographic network is drawn over OpenFreeMap's basemap, with
// its nodes shown only when zoomed in (the workshop network has 22,889).
import {
  type ErrorEvent,
  type LayerSpecification,
  LngLatBounds,
  Map as MapLibreMap,
  Marker,
  setWorkerUrl,
} from 'maplibre-gl';
import 'maplibre-gl/dist/maplibre-gl.css';
import mapWorkerUrl from 'maplibre-gl/dist/maplibre-gl-worker.mjs?worker&url';
import { idFilter, pathColour } from './expressions.ts';
import type { NetworkGeometry } from './geometry.ts';
import type { Highlight } from './result.ts';

// MapLibre locates its worker relative to its own module, which bundling breaks; Vite emits it.
setWorkerUrl(mapWorkerUrl);

const BASEMAP_STYLE = 'https://tiles.openfreemap.org/styles/positron';

export interface MapOptions {
  mode: 'abstract' | 'geographic';
  // Shown in the attribution control for the network's own data (geographic mode).
  attribution?: string;
}

export interface RouteMap {
  highlight(h: Highlight): void;
  select(edge: number | null): void;
  // Called on every map click: with the result edge under the pointer, or null when there is none.
  onEdgeClick(handler: (edge: number | null) => void): void;
  remove(): void;
}

function only(edge: number | null): Map<number, number> {
  return new Map(edge === null ? [] : [[edge, 0]]);
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

export async function createRouteMap(
  container: HTMLElement,
  geometry: NetworkGeometry,
  options: MapOptions,
): Promise<RouteMap> {
  const geographic = options.mode === 'geographic';
  const bounds = new LngLatBounds(geometry.bounds[0], geometry.bounds[1]);
  const [[west, south], [east, north]] = geometry.bounds;
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
    bounds,
    fitBoundsOptions: { padding: geographic ? 24 : 48 },
    maxBounds: [
      [west - padX, south - padY],
      [east + padX, north + padY],
    ],
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
  map.on('mouseenter', 'edge-hit', () => {
    map.getCanvas().style.cursor = 'pointer';
  });
  map.on('mouseleave', 'edge-hit', () => {
    map.getCanvas().style.cursor = '';
  });

  return {
    highlight(h: Highlight) {
      map.setFilter('route-edges', idFilter(h.edges));
      map.setPaintProperty('route-edges', 'line-color', pathColour(h.edges));
      map.setFilter('route-nodes', idFilter(h.nodes));
      map.setPaintProperty('route-nodes', 'circle-color', pathColour(h.nodes));
      map.setFilter('edge-hit', idFilter(h.edges));
    },
    select(edge: number | null) {
      map.setFilter('selected-edge', idFilter(only(edge)));
    },
    onEdgeClick(handler: (edge: number | null) => void) {
      map.on('click', (event) => {
        const id = map.queryRenderedFeatures(event.point, { layers: ['edge-hit'] })[0]?.properties.id;
        handler(typeof id === 'number' ? id : null);
      });
    },
    remove() {
      map.remove();
    },
  };
}
