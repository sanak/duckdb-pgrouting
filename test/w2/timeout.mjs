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

// One timeout per step, but never past an overall budget that ends before Playwright's own test
// timeout: however many steps the page runs, a hang still ends as an error naming its step.
export function stepper(stepMs, budgetMs, clock = () => Date.now()) {
  const deadline = clock() + budgetMs;
  return (promise, what) => withTimeout(promise, Math.max(0, Math.min(stepMs, deadline - clock())), what);
}
