// SPDX-License-Identifier: MIT
// Query results as plain values: the table renders them, and the map highlights the edge and node
// ids they contain, one colour per path.

export type Plain = null | boolean | number | string | Plain[];

export interface ResultSet {
  columns: string[];
  rows: Plain[][];
}

export interface Highlight {
  // id → index of the first path it appears in
  edges: Map<number, number>;
  nodes: Map<number, number>;
}

function isArrayLike(value: object): value is { toArray(): ArrayLike<unknown> } {
  return 'toArray' in value && typeof value.toArray === 'function';
}

// decimalScale is the Arrow field's scale for a DECIMAL column, whose values arrive unscaled.
export function plainValue(value: unknown, decimalScale?: number): Plain {
  if (value === null || value === undefined) return null;
  if (decimalScale !== undefined) return Number(value) / 10 ** decimalScale;
  if (typeof value === 'bigint') return Number(value);
  if (typeof value === 'number' || typeof value === 'string' || typeof value === 'boolean') return value;
  if (Array.isArray(value)) return value.map((v) => plainValue(v));
  if (typeof value === 'object' && isArrayLike(value)) return Array.from(value.toArray(), (v) => plainValue(v));
  return String(value);
}

export function formatCell(value: Plain): string {
  if (value === null) return 'NULL';
  if (Array.isArray(value)) return `[${value.map(formatCell).join(', ')}]`;
  return String(value);
}

export function summarize(result: ResultSet, limit: number): { shown: Plain[][]; text: string } {
  const total = result.rows.length;
  const shown = result.rows.slice(0, limit);
  const count = `${total.toLocaleString('en-US')} ${total === 1 ? 'row' : 'rows'}`;
  const text = total > limit ? `${count} (first ${limit.toLocaleString('en-US')} shown)` : count;
  return { shown, text };
}

export function highlightOf(result: ResultSet): Highlight {
  const edges = new Map<number, number>();
  const nodes = new Map<number, number>();
  const at = (name: string) => result.columns.indexOf(name);
  const [edge = -1, node = -1, pathId = -1, start = -1, end = -1] = [
    'edge',
    'node',
    'path_id',
    'start_vid',
    'end_vid',
  ].map(at);
  if (edge === -1 && node === -1) return { edges, nodes };

  const pathKey =
    pathId !== -1
      ? (row: Plain[]) => `p${row[pathId]}`
      : start !== -1 && end !== -1
        ? (row: Plain[]) => `${row[start]}>${row[end]}`
        : () => '';
  const paths = new Map<string, number>();

  for (const row of result.rows) {
    const key = pathKey(row);
    let index = paths.get(key);
    if (index === undefined) {
      index = paths.size;
      paths.set(key, index);
    }
    const e = edge === -1 ? null : row[edge];
    if (typeof e === 'number' && e >= 0 && !edges.has(e)) edges.set(e, index);
    const n = node === -1 ? null : row[node];
    if (typeof n === 'number' && !nodes.has(n)) nodes.set(n, index);
  }
  return { edges, nodes };
}
