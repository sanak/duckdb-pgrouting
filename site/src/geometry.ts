// SPDX-License-Identifier: MIT
// The sample graph has abstract grid coordinates. MapLibre draws only Web Mercator, so each grid
// unit is taken as METRES_PER_UNIT Mercator metres and inverse-projected to longitude/latitude;
// MapLibre's own forward projection then cancels it and the grid is drawn without distortion.
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

export interface SampleGeometry {
  edges: FeatureCollection<LineString, { id: number }>;
  nodes: FeatureCollection<Point, NodeProps>;
  bounds: [[number, number], [number, number]];
}

export function toLngLat(x: number, y: number): [number, number] {
  const mx = x * METRES_PER_UNIT;
  const my = y * METRES_PER_UNIT;
  return [((mx / EARTH_RADIUS) * 180) / Math.PI, (Math.atan(Math.sinh(my / EARTH_RADIUS)) * 180) / Math.PI];
}

export function sampleGeometry(vertices: VertexRow[], edges: EdgeRow[], points: PointRow[]): SampleGeometry {
  const at = new Map(vertices.map((v) => [v.id, v]));
  const ends = new Map<number, [VertexRow, VertexRow]>();
  const edgeFeatures: SampleGeometry['edges']['features'] = [];
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
  const nodeFeatures: SampleGeometry['nodes']['features'] = vertices.map((v) =>
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

  let [west, south, east, north] = [Infinity, Infinity, -Infinity, -Infinity];
  for (const f of nodeFeatures) {
    const [lng = 0, lat = 0] = f.geometry.coordinates;
    west = Math.min(west, lng);
    south = Math.min(south, lat);
    east = Math.max(east, lng);
    north = Math.max(north, lat);
  }
  return {
    edges: { type: 'FeatureCollection', features: edgeFeatures },
    nodes: { type: 'FeatureCollection', features: nodeFeatures },
    bounds: [
      [west, south],
      [east, north],
    ],
  };
}
