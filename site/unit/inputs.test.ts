// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict';
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { test } from 'node:test';
import type { MapInput, View } from '../src/datasets.ts';
import { parseDataset } from '../src/datasets.ts';
import {
  applyInput,
  areaKm2,
  extentRefusal,
  type Frame,
  hasInputLine,
  openingView,
  presetExtent,
  readInputs,
  writeInput,
} from '../src/inputs.ts';
import { parsePresetFile } from '../src/presets.ts';

const BBOX: MapInput = { variable: 'bbox', kind: 'extent', label: 'Use this view', maxAreaKm2: 25 };
const PT0: MapInput = { variable: 'pt0', kind: 'point', label: 'Pick start' };
const PT1: MapInput = { variable: 'pt1', kind: 'point', label: 'Pick goal' };
const ALL = [BBOX, PT0, PT1];

const PRESET = [
  '-- Next: the files.',
  'SET VARIABLE bbox = {xmin: 132.44, ymin: 34.37, xmax: 132.48, ymax: 34.41};',
  'SET VARIABLE pt0 = ST_Point(132.4757, 34.3973);',
  'SET VARIABLE pt1 = ST_Point(132.4528, 34.3925);',
  'SELECT 1;',
].join('\n');

test('reads the extent and both points', () => {
  const v = readInputs(PRESET, ALL);
  assert.deepEqual(v.extent, { xmin: 132.44, ymin: 34.37, xmax: 132.48, ymax: 34.41 });
  assert.deepEqual(v.points.get('pt0'), [132.4757, 34.3973]);
  assert.deepEqual(v.points.get('pt1'), [132.4528, 34.3925]);
});

test('a missing line gives no value', () => {
  const v = readInputs('SELECT 1;', ALL);
  assert.equal(v.extent, undefined);
  assert.equal(v.points.size, 0);
});

test('a line inside a -- comment does not count', () => {
  const v = readInputs('-- SET VARIABLE pt0 = ST_Point(1, 2);\n  --SET VARIABLE pt1 = ST_Point(3, 4);', ALL);
  assert.equal(v.points.size, 0);
  assert.equal(hasInputLine('-- SET VARIABLE pt0 = ST_Point(1, 2);', PT0), false);
});

test('for a variable set twice the first line counts, when read and when written', () => {
  const sql = 'SET VARIABLE pt0 = ST_Point(1, 2);\nSET VARIABLE pt0 = ST_Point(3, 4);';
  assert.deepEqual(readInputs(sql, [PT0]).points.get('pt0'), [1, 2]);
  assert.equal(
    writeInput(sql, PT0, [5, 6]),
    'SET VARIABLE pt0 = ST_Point(5.00000, 6.00000);\nSET VARIABLE pt0 = ST_Point(3, 4);',
  );
});

test('spacing, keyword case, signs, exponents and a trailing comment are accepted', () => {
  const sql =
    '\tset   variable  PT0=st_point( -1.5 , +2e1 ) ;  -- start\nset variable bbox={XMIN:-1,ymin:.5,xmax:2.,ymax:3};';
  const v = readInputs(sql, ALL);
  assert.deepEqual(v.points.get('pt0'), [-1.5, 20]);
  assert.deepEqual(v.extent, { xmin: -1, ymin: 0.5, xmax: 2, ymax: 3 });
});

test('a line with more after the statement is not an input line', () => {
  assert.equal(hasInputLine('SET VARIABLE pt0 = ST_Point(1, 2); SELECT 1;', PT0), false);
  assert.equal(hasInputLine('SET VARIABLE pt0 = ST_Point(1, 2) -- no semicolon', PT0), false);
  assert.equal(hasInputLine('SET VARIABLE pt00 = ST_Point(1, 2);', PT0), false);
});

test('writeInput replaces the line in place with five decimals, keeping indent and comment', () => {
  const sql = 'SELECT 0;\n  SET VARIABLE pt1 = ST_Point(1, 2);  -- goal\nSELECT 1;';
  assert.equal(
    writeInput(sql, PT1, [132.123456789, 34.1]),
    'SELECT 0;\n  SET VARIABLE pt1 = ST_Point(132.12346, 34.10000);  -- goal\nSELECT 1;',
  );
  assert.equal(
    writeInput(PRESET, BBOX, { xmin: 132.1, ymin: 34.2, xmax: 132.3, ymax: 34.4 })?.split('\n')[1],
    'SET VARIABLE bbox = {xmin: 132.10000, ymin: 34.20000, xmax: 132.30000, ymax: 34.40000};',
  );
});

test('writeInput returns null when the SQL has no line for the variable', () => {
  assert.equal(writeInput('SELECT 1;', PT0, [1, 2]), null);
  assert.equal(writeInput('SET VARIABLE pt0 = ST_Point(1, 2);', BBOX, { xmin: 0, ymin: 0, xmax: 1, ymax: 1 }), null);
});

test('writeInput refuses a value of the other kind', () => {
  assert.throws(() => writeInput(PRESET, PT0, { xmin: 0, ymin: 0, xmax: 1, ymax: 1 }), /pt0/);
  assert.throws(() => writeInput(PRESET, BBOX, [1, 2]), /bbox/);
});

test('areaKm2 is the equirectangular area the presets compute in SQL', () => {
  // The area-bbox preset's own result for this box: 16.252185976808004.
  assert.ok(Math.abs(areaKm2({ xmin: 132.44, ymin: 34.37, xmax: 132.48, ymax: 34.41 }) - 16.252186) < 1e-5);
  assert.equal(areaKm2({ xmin: 0, ymin: 0, xmax: 0, ymax: 1 }), 0);
});

test('extentRefusal says how large the view is and what the limit is', () => {
  assert.equal(extentRefusal({ xmin: 132.44, ymin: 34.37, xmax: 132.48, ymax: 34.41 }, 25), null);
  assert.equal(
    extentRefusal({ xmin: 132.4, ymin: 34.3, xmax: 132.5, ymax: 34.4 }, 25),
    'This view is 101.6 km²; the limit is 25 km². Zoom in.',
  );
});

const DATASETS = join(import.meta.dirname, '..', 'datasets');

function committed(): string[] {
  return readdirSync(DATASETS).filter((id) => existsSync(join(DATASETS, id, 'dataset.json')));
}

function read(id: string, file: string): unknown {
  return JSON.parse(readFileSync(join(DATASETS, id, file), 'utf8'));
}

test('every map input of a committed dataset has its SET VARIABLE line in a preset', () => {
  for (const id of committed()) {
    const d = parseDataset(id, read(id, 'dataset.json'));
    if (d.map.mode !== 'geographic') continue;
    const presets = parsePresetFile(id, read(id, 'presets.json')).presets;
    for (const input of d.map.inputs ?? []) {
      assert.ok(
        presets.some((p) => hasInputLine(p.sql, input)),
        `${id}: no preset has a SET VARIABLE ${input.variable} line`,
      );
    }
  }
});

test("overture's view is the area its first preset sets", () => {
  const d = parseDataset('overture', read('overture', 'dataset.json'));
  assert.ok(d.map.mode === 'geographic' && d.map.view && d.map.inputs);
  const first = parsePresetFile('overture', read('overture', 'presets.json')).presets[0];
  const [xmin, ymin, xmax, ymax] = d.map.view;
  assert.deepEqual(readInputs(first?.sql ?? '', d.map.inputs).extent, { xmin, ymin, xmax, ymax });
});

test('presetExtent writes the view into a preset that is still as committed', () => {
  const presets = [
    { label: 'Choose the area', sql: PRESET },
    { label: 'Next', sql: 'SELECT 2;' },
  ];
  const view = { xmin: 132.42318, ymin: 34.37875, xmax: 132.49682, ymax: 34.40125 };
  assert.equal(presetExtent(PRESET, ALL, presets, view), writeInput(PRESET, BBOX, view));
  // Edited (or a share link's) SQL, SQL without the line, and no extent input are left alone.
  assert.equal(presetExtent(`${PRESET}\n-- mine`, ALL, presets, view), null);
  assert.equal(presetExtent('SELECT 2;', ALL, presets, view), null);
  assert.equal(presetExtent(PRESET, [PT0, PT1], presets, view), null);
  // A view past the limit, and no view (no map), keep the committed line without a word.
  assert.equal(presetExtent(PRESET, ALL, presets, { xmin: 132.0, ymin: 34.0, xmax: 133.0, ymax: 35.0 }), null);
  assert.equal(presetExtent(PRESET, ALL, presets, null), null);
});

test('applyInput names the preset holding the line before it looks at the area', () => {
  const route = 'SET VARIABLE pt0 = ST_Point(1, 2);';
  const presets = [
    { label: 'Choose the area', sql: 'SET VARIABLE bbox = {xmin: 0, ymin: 0, xmax: 1, ymax: 1};' },
    { label: 'Route', sql: route },
  ];
  const huge = { xmin: 132.0, ymin: 34.0, xmax: 133.0, ymax: 35.0 };
  assert.deepEqual(applyInput(route, BBOX, huge, presets), {
    error: 'This query has no SET VARIABLE bbox line — it is in the preset “Choose the area”.',
  });
  assert.deepEqual(applyInput('SELECT 1;', PT0, [1, 2], []), { error: 'This query has no SET VARIABLE pt0 line.' });
});

test('applyInput refuses too large an extent and otherwise returns the rewritten SQL', () => {
  assert.deepEqual(applyInput(PRESET, BBOX, { xmin: 132.4, ymin: 34.3, xmax: 132.5, ymax: 34.4 }, []), {
    error: 'This view is 101.6 km²; the limit is 25 km². Zoom in.',
  });
  assert.deepEqual(applyInput(PRESET, PT0, [5, 6], []), { sql: writeInput(PRESET, PT0, [5, 6]) });
});

// What a map of this frame shows once it has fitted `box` inside its padding (getBounds' box).
function shownArea([xmin, ymin, xmax, ymax]: View, f: Frame): number {
  const innerWidth = f.width - 2 * f.padding;
  const innerHeight = f.height - 2 * f.padding;
  const cos = Math.cos((((ymin + ymax) / 2) * Math.PI) / 180);
  const boxAspect = ((xmax - xmin) * cos) / (ymax - ymin);
  const frameAspect = innerWidth / innerHeight;
  const spare = Math.max(boxAspect / frameAspect, frameAspect / boxAspect);
  return (areaKm2({ xmin, ymin, xmax, ymax }) * spare * f.width * f.height) / (innerWidth * innerHeight);
}

const DEFAULT_AREA: View = [132.44, 34.37, 132.48, 34.41];
const DEFAULT_AREA_KM2 = areaKm2({ xmin: 132.44, ymin: 34.37, xmax: 132.48, ymax: 34.41 });
const DEFAULT_LINE = 'SET VARIABLE bbox = {xmin: 132.44, ymin: 34.37, xmax: 132.48, ymax: 34.41};';

function assertShaped(shaped: View | undefined, frame: Frame, wanted: number): void {
  assert.ok(shaped);
  const [xmin, ymin, xmax, ymax] = shaped;
  const what = `${frame.width}x${frame.height}`;
  assert.ok(Math.abs(shownArea(shaped, frame) - wanted) < 1e-6, what);
  // Same centre; on screen (Mercator, locally cos φ per degree of longitude) the frame's shape.
  assert.ok(Math.abs((xmin + xmax) / 2 - 132.46) < 1e-9 && Math.abs((ymin + ymax) / 2 - 34.39) < 1e-9, what);
  const cos = Math.cos((34.39 * Math.PI) / 180);
  const aspect = ((xmax - xmin) * cos) / (ymax - ymin);
  assert.ok(Math.abs(aspect - (frame.width - 48) / (frame.height - 48)) < 1e-9, what);
}

// Fitting the 16 km² default area whole shows more than 25 km² in all of these frames.
const LAPTOP: Frame = { width: 618, height: 386, padding: 24 };
const MAXIMIZED: Frame = { width: 1258, height: 450, padding: 24 };
const LARGE: Frame = { width: 2540, height: 1300, padding: 24 };
const PHONE: Frame = { width: 358, height: 600, padding: 24 };
const FRAMES = [LAPTOP, MAXIMIZED, LARGE, PHONE];

test('openingView reshapes an area that would show more than the limit, at any window size', () => {
  for (const frame of FRAMES) {
    // The preset's own line and the dataset's view (SQL without a line) open the same way.
    assertShaped(openingView(DEFAULT_LINE, ALL, DEFAULT_AREA, frame), frame, DEFAULT_AREA_KM2);
    assertShaped(openingView('SELECT 1;', ALL, DEFAULT_AREA, frame), frame, DEFAULT_AREA_KM2);
  }
  // An area larger than the limit (typed by hand) opens on the limit's worth of it.
  const big = 'SET VARIABLE bbox = {xmin: 132.43, ymin: 34.36, xmax: 132.49, ymax: 34.42};';
  assertShaped(openingView(big, ALL, DEFAULT_AREA, MAXIMIZED), MAXIMIZED, 25);
});

test('openingView keeps an area whole when the frame shows it within the limit', () => {
  const squarish = { width: 600, height: 640, padding: 24 };
  assert.ok(shownArea(DEFAULT_AREA, squarish) < 25);
  assert.deepEqual(openingView(DEFAULT_LINE, ALL, DEFAULT_AREA, squarish), DEFAULT_AREA);
  const small = 'SET VARIABLE bbox = {xmin: 135.49, ymin: 34.69, xmax: 135.51, ymax: 34.71};';
  assert.deepEqual(openingView(small, ALL, DEFAULT_AREA, LARGE), [135.49, 34.69, 135.51, 34.71]);
  // No extent input, so no limit; and a frame not laid out yet.
  assert.deepEqual(openingView('SELECT 1;', [PT0], DEFAULT_AREA, MAXIMIZED), DEFAULT_AREA);
  assert.deepEqual(openingView('SELECT 1;', ALL, DEFAULT_AREA, { width: 0, height: 0, padding: 24 }), DEFAULT_AREA);
});

test('openingView prefers the area the SQL holds, so a shared area opens on screen', () => {
  const view: [number, number, number, number] = [132.44, 34.37, 132.48, 34.41];
  const moved = 'SET VARIABLE bbox = {xmin: 135.49, ymin: 34.69, xmax: 135.51, ymax: 34.71};';
  assert.deepEqual(openingView(moved, ALL, view), [135.49, 34.69, 135.51, 34.71]);
  assert.deepEqual(openingView('SELECT 1;', ALL, view), view);
  assert.equal(openingView('SELECT 1;', [], undefined), undefined);
});
