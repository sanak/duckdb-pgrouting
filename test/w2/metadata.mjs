// SPDX-License-Identifier: GPL-2.0-or-later
// Reads the metadata DuckDB appends to every extension build: the last 512 bytes hold eight 32-byte
// fields, last field first, then a 256-byte signature. A Wasm build carries the same bytes at the
// end of a custom section, so the layout is the same for both.
//
// W2 checks the extension version here rather than through duckdb_extensions(): DuckDB-Wasm reports
// an empty extension_version for an extension LOADed by URL, because its patched loader drops the
// version it parsed from this footer.

const FOOTER_SIZE = 512;
const FIELD_SIZE = 32;
const MAGIC = '4';

export function readFooter(bytes) {
  if (bytes.length < FOOTER_SIZE) {
    throw new Error(`not a DuckDB extension: shorter than the ${FOOTER_SIZE}-byte footer`);
  }
  const metadata = bytes.subarray(bytes.length - FOOTER_SIZE, bytes.length - FOOTER_SIZE / 2);
  const fields = Array.from({ length: 8 }, (_, i) =>
    Buffer.from(metadata.subarray(i * FIELD_SIZE, (i + 1) * FIELD_SIZE))
      .toString('latin1')
      .replace(/\0+$/, ''),
  ).reverse();
  if (fields[0] !== MAGIC) throw new Error(`not a DuckDB extension: magic value '${fields[0]}'`);
  return { platform: fields[1], duckdbVersion: fields[2], extensionVersion: fields[3], abi: fields[4] };
}
