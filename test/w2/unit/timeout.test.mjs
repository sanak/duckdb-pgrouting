// SPDX-License-Identifier: GPL-2.0-or-later
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { withTimeout } from '../timeout.mjs';

test('a settled promise passes through', async () => {
  assert.equal(await withTimeout(Promise.resolve(7), 1000, 'x'), 7);
});

test('a hung promise becomes a named timeout', async () => {
  await assert.rejects(withTimeout(new Promise(() => {}), 20, 'LOAD pgrouting'), /LOAD pgrouting timed out after 0.02 s/);
});
