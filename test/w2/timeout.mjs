// SPDX-License-Identifier: GPL-2.0-or-later
// Bounds one step of the page so that a hang (a threaded extension load has hung before,
// duckdb/duckdb-wasm#2288) ends as an error naming the step, not as Playwright's test timeout.

export function withTimeout(promise, ms, what) {
  let timer;
  const timeout = new Promise((_, reject) => {
    timer = setTimeout(() => {
      const error = new Error(`${what} timed out after ${ms / 1000} s`);
      error.name = 'StepTimeout';
      reject(error);
    }, ms);
  });
  return Promise.race([promise, timeout]).finally(() => clearTimeout(timer));
}

// One timeout per step, but never past an overall budget that ends before Playwright's own test
// timeout: however many steps the page runs, a hang still ends as an error naming its step.
export function stepper(stepMs, budgetMs, clock = () => Date.now()) {
  const deadline = clock() + budgetMs;
  return (promise, what) => withTimeout(promise, Math.max(0, Math.min(stepMs, deadline - clock())), what);
}

// The text W2 reports for a failed run. Only a timeout on the threads bundle gets the pointer to
// the known upstream hang; any other error is reported as DuckDB worded it.
export function errorText(error, bundle) {
  const text = String(error?.message ?? error);
  if (bundle !== 'threads' || error?.name !== 'StepTimeout') return text;
  return `${text} (bundle threads; a threads hang on extension load is duckdb/duckdb-wasm#2288)`;
}
