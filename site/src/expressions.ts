// SPDX-License-Identifier: MIT
// MapLibre expressions for the result layers: which features to show, and each one's path colour.
import type { ExpressionSpecification } from 'maplibre-gl';

export const PATH_COLOURS: readonly string[] = ['#d7263d', '#1b998b', '#2e86ab', '#f49d37', '#8e44ad', '#3d5a80'];

function colour(pathIndex: number): string {
  return PATH_COLOURS[pathIndex % PATH_COLOURS.length] ?? '#000000';
}

export function idFilter(ids: Map<number, number>): ExpressionSpecification {
  return ['in', ['get', 'id'], ['literal', [...ids.keys()]]];
}

export function pathColour(ids: Map<number, number>): ExpressionSpecification | string {
  if (ids.size === 0) return colour(0);
  const pairs = [...ids].flatMap(([id, pathIndex]) => [id, colour(pathIndex)]);
  // The type wants at least one label/output pair up front, which a spread cannot show; the empty
  // case returned above.
  return ['match', ['get', 'id'], ...pairs, colour(0)] as unknown as ExpressionSpecification;
}
