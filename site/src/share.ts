// SPDX-License-Identifier: MIT
// A share link carries the editor's SQL in the URL fragment, which never reaches the server:
// #q=<base64url of the UTF-8 text, unpadded>.

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

export function queryFromHash(hash: string): string | null {
  const encoded = new URLSearchParams(hash.replace(/^#/, '')).get('q');
  return encoded === null ? null : decodeQuery(encoded);
}

export function hashForQuery(sql: string): string {
  return `#q=${encodeQuery(sql)}`;
}
