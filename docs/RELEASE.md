# Release procedure

A release is a tag on `main`. It produces a GitHub Release that holds the nine platform
builds of the extension for one DuckDB version. The Release is created as a draft and published by
a person.

## Background

- **Two lines.** `main` is the stable line, built against the current DuckDB v1.5 release; it is
  what users install. The `v2.0-cyanoptera` branch is built against DuckDB's branch of that name
  and is not released until DuckDB 2.0.0 ships; then it is merged into `main`, after a v1.5
  maintenance branch has been cut from `main`.
- **Changes land on `main` only.** `v2.0-cyanoptera` is `main` plus three commits: its DuckDB
  and extension-ci-tools submodule pins with the build and CI lines that go with them, the v2.0
  source adaptations, and the one test only v2.0 can run (`test/sql/dijkstra_interrupt.test`).
  It is rebased onto `main` and force-pushed only before a release tag, when its DuckDB pin
  moves, and when DuckDB 2.0.0 ships; between those it lags `main`. `Checks.yml` enforces the
  test parity against the `main` commit it was rebased onto.
- **One DuckDB version per Release.** An extension loads only into the DuckDB version it was built
  for. Asset names carry that version after the first `.` (DuckDB names an extension after its
  file name up to the first `.`):
  `pgrouting.<duckdb version>.<platform>.duckdb_extension.gz` (native, gzipped) and
  `pgrouting.<duckdb version>.<variant>.duckdb_extension.wasm` (Wasm).
- **The version a build reports** (`duckdb_extensions().extension_version`) is the tag only when
  the build's checkout contains the tag and the tag points at the built commit; otherwise it is
  the commit hash. Pushing the tag starts its own distribution run for exactly that reason, and
  the release job refuses a build that does not report the tag.
- **Drafts.** The release job creates the Release as a draft. A Release published by a workflow's
  own token would not start other workflows on publication; one published by a person does.

## Cutting a release

1. **Both lines green.** On the commits to release, all three workflows are green on
   `main` (Main Extension Distribution Pipeline, Wasm Tests, Checks). Rebase
   `v2.0-cyanoptera` onto that commit, force-push it, and wait for the same three workflows to be
   green there too. Checks includes both generators in `--check` mode, so
   fixtures and the generated docquery tests are current.

   Also run the Playground presets that read from third-party servers, which CI skips:
   `PGROUTING_NETWORK_TESTS=1 python3 -m unittest scripts/tests/test_site_presets.py` with a
   release build of the commit (a few minutes; reads Overture Maps' servers). A failure means
   Overture changed its data: fix the presets in `site/datasets/overture/` first.
2. **W2 against the branch build.** Run the W2 workflow in `artifact` mode with the `main`
   distribution run's id:
   `gh workflow run W2.yml --ref main -f mode=artifact -f run_id=<run id>`. It must
   pass. W2 checks `pgr_version()` against the pgRouting version of its own checkout, so it runs
   from the line that built the binaries.
3. **Tag the stable-line head** with an annotated tag and push it:
   ```bash
   git switch main && git pull --ff-only
   git tag -a vX.Y.Z -m "vX.Y.Z"
   git push origin vX.Y.Z
   ```
   The tag's distribution run builds and tests all nine platforms again, then its `Draft GitHub
   Release` job verifies the linux_amd64 build in the released DuckDB CLI (the reported version
   must equal the tag, `pgr_version()` the bundled pgRouting version) and creates the draft.
4. **Review the draft**: nine assets plus `SHA256SUMS`, the title names the DuckDB version, and
   the notes list one `INSTALL` line per native platform. Then run W2 against the draft:
   `gh workflow run W2.yml --ref main -f mode=release -f tag=vX.Y.Z`. It must pass; it
   also checks that the Wasm build's embedded metadata names the tag as the extension version
   (DuckDB-Wasm itself reports an empty version for an extension loaded by URL).
5. **Publish** the draft on GitHub. Then, on one platform:
   ```sql
   -- duckdb -unsigned
   INSTALL '<the INSTALL URL for this platform from the notes>';
   LOAD pgrouting;
   SELECT extension_version FROM duckdb_extensions() WHERE extension_name = 'pgrouting';  -- vX.Y.Z
   SELECT pgr_version();
   ```
   Then redeploy the Playground, which copies the Wasm builds of published Releases only, and
   check it with W2:
   ```bash
   gh workflow run Pages.yml --ref main          # wait for it to finish
   gh workflow run W2.yml --ref main -f mode=site
   ```

If the release job fails after the draft was created, re-run the job: it replaces its own draft.
It never touches a published Release; a mistake in a published Release is fixed by a new tag.

## A new DuckDB patch release

After DuckDB publishes a new v1.5.x:

1. On `main`, change the stable job's `duckdb_version` in `MainDistributionPipeline.yml` to the
   new version, and move the `duckdb/` submodule to the new release tag (`git -C duckdb checkout
   vX.Y.Z` and commit the pointer), by pull request. Check whether the new tag still writes the
   `--match` pattern in quotes (`duckdb/extension/extension_build_tools.cmake`); while it does,
   `cmake/extension_version.cmake` stays.
2. In a second pull request, move the `@duckdb/duckdb-wasm` pin of `site/package.json` and
   `test/w2/package.json` together (`site/unit/pin.test.ts` keeps them equal) to a build that
   embeds the new DuckDB version (check with `SELECT version()` under Node). Do not merge it yet:
   `Pages.yml` deploys every push to `main`, and the Playground loads
   `wasm/<its DuckDB version>/…`, which exists only once a Release for that version is published.
3. Cut a new release (a new patch tag) as above, except that W2's `artifact` and `release` runs
   (steps 2 and 4) use `--ref <the pin pull request's branch>` instead of `--ref main`, since
   `main`'s W2 still pins the old DuckDB-Wasm. After publishing, merge the pin pull request instead
   of dispatching `Pages.yml`: its push redeploys the Playground with the new Wasm builds; then run
   W2 in `site` mode from `main`.
4. Keep the older Releases: users of the older DuckDB version still need them, and DuckDB-Wasm
   often embeds an older DuckDB version than the newest native release.

## Community extensions (later)

Registration in DuckDB's community-extensions repository waits until the pgRouting project has
responded to a courtesy notice about the use of its name. When it happens:

1. Open the descriptor pull request, with `repo.ref` at the released commit on `main` and
   `repo.ref_next` at the head of `v2.0-cyanoptera`. Every later rebase replaces that commit, so
   move `repo.ref_next` to the new head each time.
2. After the community build is published, verify `INSTALL pgrouting FROM community` on
   shell.duckdb.org and run W2 in `community` mode.
3. Make `FROM community` the default install instruction in the README and on the site.
4. **Before DuckDB 2.0.0 is released**, cut the v1.5 maintenance branch, rebase
   `v2.0-cyanoptera` onto `main` a last time, fast-forward `main` to it, point `repo.ref` at it
   and drop `repo.ref_next`.
