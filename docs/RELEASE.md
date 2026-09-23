# Release procedure

A release is a tag on the stable line. It produces a GitHub Release that holds the nine platform
builds of the extension for one DuckDB version. The Release is created as a draft and published by
a person.

## Background

- **Two lines.** `v1.5-variegata` is the stable line, built against the current DuckDB v1.5
  release; it is what users install. `main` is built against DuckDB's `v2.0-cyanoptera` branch
  and is not released until DuckDB 2.0.0 ships; from then on `main` is the stable line and tags
  move there.
- **Changes land on `main` first** and are cherry-picked onto `v1.5-variegata`. The branch differs
  from `main` only in its DuckDB and extension-ci-tools submodule pins, the v1.5 source
  adaptations, the two `if: false` lines of `MainDistributionPipeline.yml`, and the one test v1.5
  cannot run (`test/sql/dijkstra_interrupt.test`). `Checks.yml` enforces the test parity.
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
  own token would not start other workflows (such as a site deploy) on publication; one published
  by a person does.

## Cutting a release

1. **Both lines green.** On the commits to release, all three workflows are green on
   `v1.5-variegata` (Main Extension Distribution Pipeline, Wasm Tests, Checks), and the same
   change is green on `main`. Checks includes both generators in `--check` mode, so fixtures and
   the generated docquery tests are current.
2. **W2 against the branch build.** Run the W2 workflow from the stable line in `artifact` mode
   with the `v1.5-variegata` distribution run's id:
   `gh workflow run W2.yml --ref v1.5-variegata -f mode=artifact -f run_id=<run id>`. It must
   pass. W2 checks `pgr_version()` against the pgRouting version of its own checkout, so it runs
   from the line that built the binaries (without `--ref` it runs from `main`).
3. **Tag the stable-line head** with an annotated tag and push it:
   ```bash
   git switch v1.5-variegata && git pull --ff-only
   git tag -a vX.Y.Z -m "vX.Y.Z"
   git push origin vX.Y.Z
   ```
   The tag's distribution run builds and tests all nine platforms again, then its `Draft GitHub
   Release` job verifies the linux_amd64 build in the released DuckDB CLI (the reported version
   must equal the tag, `pgr_version()` the bundled pgRouting version) and creates the draft.
4. **Review the draft**: nine assets plus `SHA256SUMS`, the title names the DuckDB version, and
   the notes list one `INSTALL` line per native platform. Then run W2 against the draft:
   `gh workflow run W2.yml --ref v1.5-variegata -f mode=release -f tag=vX.Y.Z`. It must pass and
   report the tag as the extension version.
5. **Publish** the draft on GitHub. Then, on one platform:
   ```sql
   -- duckdb -unsigned
   INSTALL '<the INSTALL URL for this platform from the notes>';
   LOAD pgrouting;
   SELECT extension_version FROM duckdb_extensions() WHERE extension_name = 'pgrouting';  -- vX.Y.Z
   SELECT pgr_version();
   ```
   When the documentation site serves the Wasm builds, confirm its deploy picked the new Release
   up and run W2 in `site` mode.

If the release job fails after the draft was created, re-run the job: it replaces its own draft.
It never touches a published Release; a mistake in a published Release is fixed by a new tag.

## A new DuckDB patch release

After DuckDB publishes a new v1.5.x:

1. On `main`, change the disabled stable job's `duckdb_version` in
   `MainDistributionPipeline.yml` to the new version, by pull request.
2. Cherry-pick that commit onto `v1.5-variegata`, and move its `duckdb/` submodule to the new
   release tag (`git -C duckdb checkout vX.Y.Z` and commit the pointer). Push and wait for all
   three workflows.
3. Cut a new release (a new patch tag) as above. Keep the older Releases: users of the older DuckDB
   version still need them, and DuckDB-Wasm often embeds an older DuckDB version than the newest
   native release.

## Community extensions (later)

Registration in DuckDB's community-extensions repository waits until the pgRouting project has
responded to a courtesy notice about the use of its name. When it happens:

1. Open the descriptor pull request, with `repo.ref` at the stable line's released commit and
   `repo.ref_next` at `main`.
2. After the community build is published, verify `INSTALL pgrouting FROM community` on
   shell.duckdb.org and run W2 in `community` mode.
3. Make `FROM community` the default install instruction in the README and on the site.
4. **Before DuckDB 2.0.0 is released**, repoint `repo.ref` at the release commit on `main` and drop
   `repo.ref_next`: the release-day rebuild builds `ref` against the new stable version, which the
   v1.5 branch cannot build. `v1.5-variegata` is frozen after that.
