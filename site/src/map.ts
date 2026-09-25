// SPDX-License-Identifier: MIT
// The sample graph on a tile-less MapLibre map: a background, the grey network, two result layers
// whose filter and colour follow each query's edge and node ids, an outline under the selected
// edge, and a wide invisible layer that makes the result's edges easy to click.
import { LngLatBounds, Map as MapLibreMap, Marker, setWorkerUrl } from 'maplibre-gl';
import 'maplibre-gl/dist/maplibre-gl.css';
import mapWorkerUrl from 'maplibre-gl/dist/maplibre-gl-worker.mjs?worker&url';
import { idFilter, pathColour } from './expressions.ts';
import type { SampleGeometry } from './geometry.ts';
import type { Highlight } from './result.ts';

// MapLibre locates its worker relative to its own module, which bundling breaks; Vite emits it.
setWorkerUrl(mapWorkerUrl);

export interface RouteMap {
  highlight(h: Highlight): void;
  select(edge: number | null): void;
  // Called on every map click: with the result edge under the pointer, or null when there is none.
  onEdgeClick(handler: (edge: number | null) => void): void;
}

function only(edge: number | null): Map<number, number> {
  return new Map(edge === null ? [] : [[edge, 0]]);
}

export async function createRouteMap(container: HTMLElement, geometry: SampleGeometry): Promise<RouteMap> {
  const bounds = new LngLatBounds(geometry.bounds[0], geometry.bounds[1]);
  const [[west, south], [east, north]] = geometry.bounds;
  const padX = (east - west) * 0.5;
  const padY = (north - south) * 0.5;
  const empty = new Map<number, number>();

  const map = new MapLibreMap({
    container,
    style: {
      version: 8,
      sources: {
        edges: { type: 'geojson', data: geometry.edges },
        nodes: { type: 'geojson', data: geometry.nodes },
      },
      layers: [
        { id: 'background', type: 'background', paint: { 'background-color': '#f7f7f4' } },
        { id: 'edges', type: 'line', source: 'edges', paint: { 'line-color': '#b5b5b5', 'line-width': 3 } },
        {
          id: 'selected-edge',
          type: 'line',
          source: 'edges',
          filter: idFilter(empty),
          layout: { 'line-cap': 'round' },
          paint: { 'line-color': '#1f2328', 'line-width': 13 },
        },
        {
          id: 'route-edges',
          type: 'line',
          source: 'edges',
          filter: idFilter(empty),
          layout: { 'line-cap': 'round' },
          paint: { 'line-color': pathColour(empty), 'line-width': 7, 'line-opacity': 0.85 },
        },
        {
          id: 'nodes',
          type: 'circle',
          source: 'nodes',
          paint: {
            'circle-radius': ['match', ['get', 'kind'], 'point', 4, 5],
            'circle-color': ['match', ['get', 'kind'], 'point', '#ffffff', '#6b6b6b'],
            'circle-stroke-color': '#6b6b6b',
            'circle-stroke-width': 1.5,
          },
        },
        {
          id: 'route-nodes',
          type: 'circle',
          source: 'nodes',
          filter: idFilter(empty),
          paint: {
            'circle-radius': 7,
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
      ],
    },
    bounds,
    fitBoundsOptions: { padding: 48 },
    maxBounds: [
      [west - padX, south - padY],
      [east + padX, north + padY],
    ],
    renderWorldCopies: false,
    dragRotate: false,
    pitchWithRotate: false,
    touchPitch: false,
    attributionControl: false,
  });
  map.touchZoomRotate.disableRotation();

  // Labels as HTML markers, so the style needs no glyphs endpoint.
  for (const f of geometry.nodes.features) {
    const label = document.createElement('div');
    label.className = `node-label ${f.properties.kind}`;
    label.textContent = f.properties.label;
    const [lng = 0, lat = 0] = f.geometry.coordinates;
    new Marker({ element: label, anchor: 'bottom-left', offset: [5, -3] }).setLngLat([lng, lat]).addTo(map);
  }

  await map.once('load');
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
  };
}
