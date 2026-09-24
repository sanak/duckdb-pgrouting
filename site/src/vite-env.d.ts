// SPDX-License-Identifier: MIT
/// <reference types="vite/client" />

interface ImportMetaEnv {
  // Dev server only: load the extension from this URL instead of the site's wasm/ directory.
  readonly VITE_PGROUTING_EXTENSION_URL?: string;
}
