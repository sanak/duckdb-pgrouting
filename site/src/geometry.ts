// SPDX-License-Identifier: MIT
// The sample graph has abstract grid coordinates; the other datasets are longitude/latitude.
// MapLibre draws only Web Mercator, so each grid unit is taken as METRES_PER_UNIT Mercator metres
// and inverse-projected to longitude/latitude; MapLibre's own forward projection then cancels it
// and the grid is drawn without distortion.
import type { FeatureCollection, LineString, Point, Position } from 'geojson';

export const EARTH_RADIUS = 6378137;
export const METRES_PER_UNIT = 1000;

export interface VertexRow {
  id: number;
  x: number;
  y: number;
}

export interface EdgeRow {
  id: number;
  source: number;
  target: number;
}

export interface PointRow {
  pid: number;
  edge_id: number;
  fraction: number;
}

export type NodeProps = { id: number; label: string; kind: 'vertex' | 'point' };

export interface NetworkGeometry {
  edges: FeatureCollection<LineString, { id: number }>;
  nodes: FeatureCollection<Point, NodeProps>;
  bounds: [[number, number], [number, number]];
}

export function toLngLat(x: number, y: number): [number, number] {
  const mx = x * METRES_PER_UNIT;
  const my = y * METRES_PER_UNIT;
  return [((mx / EARTH_RADIUS) * 180) / Math.PI, (Math.atan(Math.sinh(my / EARTH_RADIUS)) * 180) / Math.PI];
}

export function sampleGeometry(vertices: VertexRow[], edges: EdgeRow[], points: PointRow[]): NetworkGeometry {
  const at = new Map(vertices.map((v) => [v.id, v]));
  const ends = new Map<number, [VertexRow, VertexRow]>();
  const edgeFeatures: NetworkGeometry['edges']['features'] = [];
  for (const e of edges) {
    const a = at.get(e.source);
    const b = at.get(e.target);
    if (!a || !b) continue;
    ends.set(e.id, [a, b]);
    edgeFeatures.push({
      type: 'Feature',
      properties: { id: e.id },
      geometry: { type: 'LineString', coordinates: [toLngLat(a.x, a.y), toLngLat(b.x, b.y)] },
    });
  }

  const node = (id: number, label: string, kind: NodeProps['kind'], position: Position) =>
    ({ type: 'Feature', properties: { id, label, kind }, geometry: { type: 'Point', coordinates: position } }) as const;
  const nodeFeatures: NetworkGeometry['nodes']['features'] = vertices.map((v) =>
    node(v.id, String(v.id), 'vertex', toLngLat(v.x, v.y)),
  );
  for (const p of points) {
    const edge = ends.get(p.edge_id);
    if (!edge) continue;
    const [a, b] = edge;
    const x = a.x + p.fraction * (b.x - a.x);
    const y = a.y + p.fraction * (b.y - a.y);
    // withPoints reports a point of interest as the node -pid.
    nodeFeatures.push(node(-p.pid, String(-p.pid), 'point', toLngLat(x, y)));
  }

  return {
    edges: { type: 'FeatureCollection', features: edgeFeatures },
    nodes: { type: 'FeatureCollection', features: nodeFeatures },
    bounds: boundsOf(nodeFeatures.map((f) => f.geometry.coordinates)),
  };
}

export interface GeoEdgeRow {
  id: number;
  geojson: string; // ST_AsGeoJSON of a LineString
}

export interface GeoNodeRow {
  id: number;
  x: number; // longitude
  y: number; // latitude
}

function boundsOf(positions: Iterable<Position>): NetworkGeometry['bounds'] {
  let [west, south, east, north] = [Infinity, Infinity, -Infinity, -Infinity];
  for (const [lng = 0, lat = 0] of positions) {
    west = Math.min(west, lng);
    south = Math.min(south, lat);
    east = Math.max(east, lng);
    north = Math.max(north, lat);
  }
  // With no edge and no node, west/east/south/north stay infinite: MapLibre would otherwise be
  // given a bounding box it cannot fit to, and fail with an error that does not say why.
  if (!Number.isFinite(west) || !Number.isFinite(south) || !Number.isFinite(east) || !Number.isFinite(north)) {
    throw new Error('the map has nothing to draw');
  }
  return [
    [west, south],
    [east, north],
  ];
}

function lineString(geojson: string): LineString | null {
  try {
    const parsed = JSON.parse(geojson) as { type?: unknown; coordinates?: unknown };
    return parsed.type === 'LineString' && Array.isArray(parsed.coordinates) ? (parsed as LineString) : null;
  } catch {
    return null;
  }
}

export function geographicGeometry(edges: GeoEdgeRow[], nodes: GeoNodeRow[]): NetworkGeometry {
  const edgeFeatures: NetworkGeometry['edges']['features'] = [];
  for (const e of edges) {
    const geometry = lineString(e.geojson);
    if (geometry) edgeFeatures.push({ type: 'Feature', properties: { id: e.id }, geometry });
  }
  const nodeFeatures: NetworkGeometry['nodes']['features'] = nodes.map((n) => ({
    type: 'Feature',
    properties: { id: n.id, label: String(n.id), kind: 'vertex' },
    geometry: { type: 'Point', coordinates: [n.x, n.y] },
  }));
  const positions = [
    ...edgeFeatures.flatMap((f) => f.geometry.coordinates),
    ...nodeFeatures.map((f) => f.geometry.coordinates),
  ];
  return {
    edges: { type: 'FeatureCollection', features: edgeFeatures },
    nodes: { type: 'FeatureCollection', features: nodeFeatures },
    bounds: boundsOf(positions),
  };
}
