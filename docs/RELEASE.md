# Release procedure

A release is a tag on `main`. It produces a GitHub Release that holds the nine platform
builds of the extension for one DuckDB version. The Release is created as a draft and published by
a person.

## Background

- **Two lines.** `main` is the stable line, built against the current DuckDB v1.5 release; it is
  what users install. The `v2.0-cyanoptera` branch is built against DuckDB's branch of that name
  and is not released until DuckDB 2.0.0 ships; then it is merged into `main`, after a v1.5
  maintenance branch has been cut from `main`.
- **Changes land on `main` first** and are cherry-picked onto `v2.0-cyanoptera`. The branch
  differs from `main` only in its DuckDB and extension-ci-tools submodule pins, the v2.0 source
  adaptations, the two `if: false` lines of `MainDistributionPipeline.yml`, and the one test only
  v2.0 can run (`test/sql/dijkstra_interrupt.test`). `Checks.yml` enforces the test parity.
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
   `main` (Main Extension Distribution Pipeline, Wasm Tests, Checks), and the same
   change is green on `v2.0-cyanoptera`. Checks includes both generators in `--check` mode, so
   fixtures and the generated docquery tests are current.
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
   vX.Y.Z` and commit the pointer), by pull request.
2. Cut a new release (a new patch tag) as above. Keep the older Releases: users of the older DuckDB
   version still need them, and DuckDB-Wasm often embeds an older DuckDB version than the newest
   native release.

## Community extensions (later)

Registration in DuckDB's community-extensions repository waits until the pgRouting project has
responded to a courtesy notice about the use of its name. When it happens:

1. Open the descriptor pull request, with `repo.ref` at the released commit on `main` and
   `repo.ref_next` at `v2.0-cyanoptera`.
2. After the community build is published, verify `INSTALL pgrouting FROM community` on
   shell.duckdb.org and run W2 in `community` mode.
3. Make `FROM community` the default install instruction in the README and on the site.
4. **Before DuckDB 2.0.0 is released**, cut the v1.5 maintenance branch, merge `v2.0-cyanoptera`
   into `main`, point `repo.ref` at it and drop `repo.ref_next`.
