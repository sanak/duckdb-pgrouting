// SPDX-License-Identifier: MIT
// Starting points for the editor. Each dataset has a presets.json next to its dataset.json:
// { license, licenseUrl?, attribution?, source?, presets: [{ id, group, label, sql, source? }] }. `sql` may be an
// array of lines, which reads better in JSON. The presets of a file are in the order a reader runs
// them: one that needs a table, view or macro only ever needs one an earlier preset creates.
import { type Json, list, object, text } from './json.ts';

export interface Preset {
  id: string;
  group: string;
  label: string;
  sql: string;
  source?: string;
}

export interface PresetFile {
  license: string;
  licenseUrl?: string;
  attribution?: string;
  source?: string;
  presets: Preset[];
}

const PRESET_ID = /^[a-z0-9]+(?:-[a-z0-9]+)*$/;

function optionalLink(o: Json, key: string, where: string): string | undefined {
  if (o[key] === undefined) return undefined;
  const value = text(o, key, where);
  if (!value.startsWith('https://')) throw new Error(`${where}.${key}: expected an https:// link`);
  return value;
}

function sqlOf(o: Json, where: string): string {
  const value = o.sql;
  if (typeof value === 'string' && value.trim() !== '') return value;
  if (Array.isArray(value) && value.length > 0 && value.every((line) => typeof line === 'string'))
    return value.join('\n');
  throw new Error(`${where}.sql: expected a non-empty string or an array of lines`);
}

export function parsePresetFile(where: string, value: unknown): PresetFile {
  const o = object(value, where);
  const presets = list(o, 'presets', where).map((p, i): Preset => {
    const w = `${where}.presets[${i}]`;
    const preset = object(p, w);
    const id = text(preset, 'id', w);
    if (!PRESET_ID.test(id)) throw new Error(`${w}.id: '${id}' must be lower-case words joined by '-'`);
    const source = optionalLink(preset, 'source', w);
    return {
      id,
      group: text(preset, 'group', w),
      label: text(preset, 'label', w),
      sql: sqlOf(preset, w),
      ...(source ? { source } : {}),
    };
  });
  const attribution = o.attribution === undefined ? undefined : text(o, 'attribution', where);
  const source = optionalLink(o, 'source', where);
  const licenseUrl = optionalLink(o, 'licenseUrl', where);
  return {
    license: text(o, 'license', where),
    ...(licenseUrl ? { licenseUrl } : {}),
    ...(attribution ? { attribution } : {}),
    ...(source ? { source } : {}),
    presets,
  };
}

export function presetGroups(presets: readonly Preset[]): { group: string; presets: Preset[] }[] {
  const groups = new Map<string, Preset[]>();
  for (const p of presets) {
    const members = groups.get(p.group) ?? [];
    members.push(p);
    groups.set(p.group, members);
  }
  return [...groups].map(([group, members]) => ({ group, presets: members }));
}

const CREATES =
  /\bCREATE\s+(?:OR\s+REPLACE\s+)?(?:TEMP(?:ORARY)?\s+)?(?:TABLE|VIEW|MACRO)\s+(?:IF\s+NOT\s+EXISTS\s+)?"?([A-Za-z_]\w*)"?/gi;

// Lower-case names of the tables, views and macros the SQL creates.
export function createdNames(sql: string): string[] {
  return [...sql.matchAll(CREATES)].map((m) => (m[1] ?? '').toLowerCase());
}

// The name in DuckDB's "Table with name x does not exist!" (tables and views) or "Table Function with
// name x does not exist!" (table macros), lower-cased; null for any other error.
export function missingName(message: string): string | null {
  const match = /with name "?([A-Za-z_]\w*)"? does not exist/.exec(message);
  return match?.[1] ? match[1].toLowerCase() : null;
}

export function prerequisiteHint(message: string, presets: readonly Preset[], current: string | null): string | null {
  const name = missingName(message);
  if (name === null) return null;
  const creator = presets.find((p) => createdNames(p.sql).includes(name));
  if (!creator || creator.id === current) return null;
  return `Run the preset “${creator.label}” first.`;
}
