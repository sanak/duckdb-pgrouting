// SPDX-License-Identifier: MIT
// A share link carries the dataset and the editor's SQL in the URL fragment, which never reaches the
// server: #d=<dataset id>&q=<base64url of the UTF-8 text, unpadded>. Links made before datasets
// existed have no d; they open the default dataset.

export function encodeQuery(sql: string): string {
  let binary = '';
  for (const byte of new TextEncoder().encode(sql)) binary += String.fromCharCode(byte);
  return btoa(binary).replaceAll('+', '-').replaceAll('/', '_').replace(/=+$/, '');
}

export function decodeQuery(encoded: string): string | null {
  if (!/^[A-Za-z0-9_-]*$/.test(encoded)) return null;
  try {
    const base64 = encoded.replaceAll('-', '+').replaceAll('_', '/');
    const binary = atob(base64 + '='.repeat((4 - (base64.length % 4)) % 4));
    const bytes = Uint8Array.from(binary, (c) => c.charCodeAt(0));
    return new TextDecoder('utf-8', { fatal: true }).decode(bytes);
  } catch {
    return null;
  }
}

// `dataset` is the raw decoded `d`, whatever its shape: an id the index does not list and one that
// could never be a dataset id (DATASET_ID) are both simply unknown, and the caller reports either
// the same way against its own index instead of this module silently dropping the malformed ones.
export function stateFromHash(hash: string): { dataset: string | null; sql: string | null } {
  const params = new URLSearchParams(hash.replace(/^#/, ''));
  const d = params.get('d');
  const q = params.get('q');
  return { dataset: d, sql: q === null ? null : decodeQuery(q) };
}

export function hashFor(dataset: string, sql: string): string {
  return `#d=${dataset}&q=${encodeQuery(sql)}`;
}
