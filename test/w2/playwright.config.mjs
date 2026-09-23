// SPDX-License-Identifier: GPL-2.0-or-later
import { defineConfig, devices } from '@playwright/test';

const port = Number(process.env.W2_PORT || 4173);

export default defineConfig({
  testDir: './tests',
  // Instantiating DuckDB-Wasm and fetching a ~40 MB module takes seconds, not minutes.
  timeout: 120_000,
  reporter: 'list',
  use: { baseURL: `http://127.0.0.1:${port}` },
  projects: [{ name: 'chromium', use: { ...devices['Desktop Chrome'] } }],
  webServer: { command: 'node server.mjs', url: `http://127.0.0.1:${port}/`, reuseExistingServer: false },
});
