// SPDX-License-Identifier: MIT
// A map input (dataset.json map.inputs) lives in the editor's SQL as one line:
//   SET VARIABLE bbox = {xmin: 132.44, ymin: 34.37, xmax: 132.48, ymax: 34.41};
//   SET VARIABLE pt0 = ST_Point(132.4757, 34.3973);
// The map reads its values from there and writes new ones back there, so a share link carries
// them and the reader sees every value a query uses. Only a whole line of exactly one of these two
// forms counts (any spacing and keyword case, an optional trailing -- comment); a line that is
// commented out does not, and for a variable set twice the first line wins.
import type { MapInput, View } from './datasets.ts';

export interface Box {
  xmin: number;
  ymin: number;
  xmax: number;
  ymax: number;
}

export type LonLat = [number, number];

export interface InputValues {
  extent?: Box;
  points: Map<string, LonLat>;
}

const NUMBER = String.raw`([-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?)`;
const _ = '[ \\t]*';
const FIELD = (name: string) => `${_}${name}${_}:${_}${NUMBER}${_}`;

// Group 1 is the indent, the numbers follow in order, and the last group is what trails the `;`.
function lineOf(input: MapInput): RegExp {
  const head = String.raw`^([ \t]*)SET[ \t]+VARIABLE[ \t]+${input.variable}${_}=${_}`;
  const value =
    input.kind === 'extent'
      ? String.raw`\{${FIELD('xmin')},${FIELD('ymin')},${FIELD('xmax')},${FIELD('ymax')}\}`
      : String.raw`ST_Point${_}\(${_}${NUMBER}${_},${_}${NUMBER}${_}\)`;
  return new RegExp(String.raw`${head}${value}${_};([ \t]*(?:--[^\n]*)?)$`, 'im');
}

export function hasInputLine(sql: string, input: MapInput): boolean {
  return lineOf(input).test(sql);
}

export function readInputs(sql: string, inputs: readonly MapInput[]): InputValues {
  const values: InputValues = { points: new Map() };
  for (const input of inputs) {
    const match = lineOf(input).exec(sql);
    if (!match) continue;
    if (input.kind === 'extent') {
      const [xmin = 0, ymin = 0, xmax = 0, ymax = 0] = match.slice(2, 6).map(Number);
      values.extent = { xmin, ymin, xmax, ymax };
    } else {
      const [lon = 0, lat = 0] = match.slice(2, 4).map(Number);
      values.points.set(input.variable, [lon, lat]);
    }
  }
  return values;
}

// Five decimals of a degree are about a metre.
const fixed = (n: number) => n.toFixed(5);

function statement(input: MapInput, value: Box | LonLat): string {
  if (input.kind === 'point' && Array.isArray(value)) {
    return `SET VARIABLE ${input.variable} = ST_Point(${fixed(value[0])}, ${fixed(value[1])});`;
  }
  if (input.kind === 'extent' && !Array.isArray(value)) {
    const { xmin, ymin, xmax, ymax } = value;
    return `SET VARIABLE ${input.variable} = {xmin: ${fixed(xmin)}, ymin: ${fixed(ymin)}, xmax: ${fixed(xmax)}, ymax: ${fixed(ymax)}};`;
  }
  throw new Error(`input ${input.variable}: a ${input.kind} needs a ${input.kind === 'point' ? 'point' : 'box'}`);
}

// The SQL with the input's line holding `value`, or null when the SQL has no line for it.
export function writeInput(sql: string, input: MapInput, value: Box | LonLat): string | null {
  const replacement = statement(input, value);
  const match = lineOf(input).exec(sql);
  if (!match) return null;
  const indent = match[1] ?? '';
  const tail = match[match.length - 1] ?? '';
  return sql.slice(0, match.index) + indent + replacement + tail + sql.slice(match.index + match[0].length);
}

// Equirectangular at the box's middle latitude. The area-bbox preset and the load-segments guard
// compute the same in SQL, so the button and the SQL agree.
const KM_PER_DEGREE_LATITUDE = 110.574;
const KM_PER_DEGREE_LONGITUDE_AT_EQUATOR = 111.32;

export function areaKm2(box: Box): number {
  const middle = (((box.ymin + box.ymax) / 2) * Math.PI) / 180;
  return (
    (box.xmax - box.xmin) *
    KM_PER_DEGREE_LONGITUDE_AT_EQUATOR *
    Math.cos(middle) *
    (box.ymax - box.ymin) *
    KM_PER_DEGREE_LATITUDE
  );
}

export function extentRefusal(box: Box, maxAreaKm2: number): string | null {
  const area = areaKm2(box);
  return area > maxAreaKm2 ? `This view is ${area.toFixed(1)} km²; the limit is ${maxAreaKm2} km². Zoom in.` : null;
}

// What a map button does to the editor's SQL: the rewritten SQL, or why it was left alone. A
// missing line is reported first — zooming in would not help a query that has no line to hold the
// view — and names the preset that has one.
export function applyInput(
  sql: string,
  input: MapInput,
  value: Box | LonLat,
  presets: readonly { label: string; sql: string }[],
): { sql: string } | { error: string } {
  const next = writeInput(sql, input, value);
  if (next === null) {
    const holder = presets.find((p) => hasInputLine(p.sql, input));
    const where = holder ? ` — it is in the preset “${holder.label}”` : '';
    return { error: `This query has no SET VARIABLE ${input.variable} line${where}.` };
  }
  if (input.kind === 'extent' && !Array.isArray(value)) {
    const refusal = extentRefusal(value, input.maxAreaKm2 ?? Number.POSITIVE_INFINITY);
    if (refusal) return { error: refusal };
  }
  return { sql: next };
}

// What "Use this view" would make of an area preset that is still as committed, done for the
// reader when the preset reaches the editor (the dataset opens, or the preset is chosen again):
// the preset's own box is fitted to the frame, so its outline would not match the map. Edited SQL
// (a share link's included), a view past the limit and no map at all leave the SQL alone (null).
export function presetExtent(
  sql: string,
  inputs: readonly MapInput[],
  presets: readonly { sql: string }[],
  view: Box | null,
): string | null {
  const input = inputs.find((i) => i.kind === 'extent');
  if (!input || !view || !presets.some((p) => p.sql === sql)) return null;
  if (extentRefusal(view, input.maxAreaKm2 ?? Number.POSITIVE_INFINITY)) return null;
  return writeInput(sql, input, view);
}

// The map's size in CSS pixels and the padding it fits a view inside.
export interface Frame {
  width: number;
  height: number;
  padding: number;
}

// Where a map opens while its network is empty: on the area the SQL holds (the preset's own, a
// share link's, or one typed by hand), so the reader sees the outline and builds the network on
// screen; otherwise on the dataset's view. Fitting a box into a frame of another shape shows more
// than the box; when that would pass the extent's limit, so that "Use this view" would be refused
// straight away, the box is reshaped to the frame around the same centre and the map shows the
// box's own area (at most the limit) instead, at any window size.
export function openingView(
  sql: string,
  inputs: readonly MapInput[],
  view: View | undefined,
  frame?: Frame,
): View | undefined {
  const box = readInputs(sql, inputs).extent;
  const opening: View | undefined = box ? [box.xmin, box.ymin, box.xmax, box.ymax] : view;
  const limit = inputs.find((i) => i.kind === 'extent')?.maxAreaKm2;
  if (!opening || !frame || limit === undefined) return opening;
  const inner = { width: frame.width - 2 * frame.padding, height: frame.height - 2 * frame.padding };
  if (inner.width <= 0 || inner.height <= 0) return opening;
  const [xmin, ymin, xmax, ymax] = opening;
  const area = areaKm2({ xmin, ymin, xmax, ymax });
  const y = (ymin + ymax) / 2;
  // On screen a degree of longitude is cos φ of a degree of latitude (Mercator, locally).
  const cos = Math.cos((y * Math.PI) / 180);
  const boxAspect = ((xmax - xmin) * cos) / (ymax - ymin);
  const frameAspect = inner.width / inner.height;
  // What the whole map shows: the fitted box, the frame's spare side and the padding.
  const grow = (frame.width * frame.height) / (inner.width * inner.height);
  const shown = area * Math.max(boxAspect / frameAspect, frameAspect / boxAspect) * grow;
  if (shown <= limit) return opening;
  const innerArea = Math.min(area, limit) / grow;
  const dy = Math.sqrt(innerArea / (frameAspect * KM_PER_DEGREE_LONGITUDE_AT_EQUATOR * KM_PER_DEGREE_LATITUDE));
  const dx = (frameAspect * dy) / cos;
  const x = (xmin + xmax) / 2;
  return [x - dx / 2, y - dy / 2, x + dx / 2, y + dy / 2];
}
