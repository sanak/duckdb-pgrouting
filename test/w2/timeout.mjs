// SPDX-License-Identifier: GPL-2.0-or-later
// Bounds one step of the page so that a hang (a threaded extension load has hung before,
// duckdb/duckdb-wasm#2288) ends as an error naming the step, not as Playwright's test timeout.

export function withTimeout(promise, ms, what) {
  let timer;
  const timeout = new Promise((_, reject) => {
    timer = setTimeout(() => reject(new Error(`${what} timed out after ${ms / 1000} s`)), ms);
  });
  return Promise.race([promise, timeout]).finally(() => clearTimeout(timer));
}
