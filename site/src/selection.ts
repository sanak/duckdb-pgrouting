// SPDX-License-Identifier: MIT
// The one edge selected in both the result grid and the map, and how a pick changes it.

export type PickSource = 'grid' | 'map';

// A grid pick always selects, because cell clicks also start copy ranges and must not clear the
// map. Picking the selected edge again on the map clears it. null means nothing selectable was
// picked, which clears the selection from either side.
export function nextSelection(current: number | null, picked: number | null, source: PickSource): number | null {
  if (source === 'map' && picked !== null && picked === current) return null;
  return picked;
}
