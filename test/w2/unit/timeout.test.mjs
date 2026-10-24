// SPDX-License-Identifier: GPL-2.0-or-later
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { stepper, withTimeout } from '../timeout.mjs';

test('a settled promise passes through', async () => {
  assert.equal(await withTimeout(Promise.resolve(7), 1000, 'x'), 7);
});

test('a hung promise becomes a named timeout', async () => {
  await assert.rejects(withTimeout(new Promise(() => {}), 20, 'LOAD pgrouting'), /LOAD pgrouting timed out after 0.02 s/);
});

test('a step gets at most what is left of the overall budget', async () => {
  let now = 0;
  const step = stepper(60_000, 50, () => now);
  now = 40;
  await assert.rejects(step(new Promise(() => {}), 'SELECT pgr_version()'), /SELECT pgr_version\(\) timed out after 0.01 s/);
});

test('a step within the budget gets the full step timeout and passes values through', async () => {
  const step = stepper(1000, 60_000);
  assert.equal(await step(Promise.resolve(3), 'x'), 3);
  await assert.rejects(step(new Promise(() => {}), 'LOAD spatial'), /LOAD spatial timed out after 1 s/);
});
