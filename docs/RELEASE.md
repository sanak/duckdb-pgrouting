# Release procedure

A release is a tag on `main`. It produces a GitHub Release that holds the nine platform
builds of the extension for one DuckDB version. The Release is created as a draft and published by
a person.

## Background

- **One line.** `main` is built against the current DuckDB v2.0 release; it is what users
  install, and every change lands there.
- **v1.5 is frozen.** The `v1.5-variegata` branch holds the final DuckDB v1.5 state (`v0.4.1`
  plus its Wasm-pin docs) and takes no commit other than a v1.5 fix; Releases for DuckDB v1.5
  (up to `v0.4.1`) stay published. A v1.5 fix, if one is ever needed, lands on that branch by
  pull request and is merged forward into `main` (`AGENTS.md`, *Release lines and releases*).
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

1. **`main` green.** On the commit to release, all three workflows are green on `main` (Main
   Extension Distribution Pipeline, Wasm Tests, Checks). Checks includes both generators in
   `--check` mode, so fixtures and the generated docquery tests are current.

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

After DuckDB publishes a new v2.0.x:

1. On `main`, change the build job's `duckdb_version` in `MainDistributionPipeline.yml` to the
   new version, and move the `duckdb/` submodule to the new release tag (`git -C duckdb checkout
   vX.Y.Z` and commit the pointer), by pull request.
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

1. Open the descriptor pull request, with `repo.ref` at the released commit on `main`.
2. After the community build is published, verify `INSTALL pgrouting FROM community` on
   shell.duckdb.org and run W2 in `community` mode.
3. Make `FROM community` the default install instruction in the README and on the site.
