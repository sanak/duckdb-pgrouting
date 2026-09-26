// SPDX-License-Identifier: MIT
// The few checks the Playground's JSON files need. Each error names the file and key, so a broken
// dataset or preset file is found by the unit tests rather than by a reader.

export type Json = Record<string, unknown>;

export function object(value: unknown, where: string): Json {
  if (typeof value !== 'object' || value === null || Array.isArray(value))
    throw new Error(`${where}: expected an object`);
  return value as Json;
}

export function text(o: Json, key: string, where: string): string {
  const value = o[key];
  if (typeof value !== 'string' || value.trim() === '') throw new Error(`${where}.${key}: expected a non-empty string`);
  return value;
}

export function list(o: Json, key: string, where: string): unknown[] {
  const value = o[key];
  if (!Array.isArray(value)) throw new Error(`${where}.${key}: expected an array`);
  return value;
}
