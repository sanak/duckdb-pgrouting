# SPDX-License-Identifier: GPL-2.0-or-later
"""Turn one distribution run's artifacts into the assets of a GitHub Release.

The distribution workflow uploads one artifact per platform, each holding a file named after the
extension alone. A Release holds all of them side by side, so each asset carries the DuckDB version
and the platform too -- after the first '.', because DuckDB installs a file under the name up to its
first '.', and that must stay "pgrouting". Native builds are gzipped (DuckDB's INSTALL recognises
the gzip magic); Wasm builds are published as built.

Before anything is written, the linux_amd64 build is loaded into the released DuckDB CLI of the
same version, and its reported extension version must equal the tag: a build whose checkout did
not see the tag reports a commit hash instead, and must never be published under the tag.
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import pathlib
import re
import shutil
import sys
from dataclasses import dataclass
from typing import List, Sequence, Tuple

import duckdbcli

EXTENSION = "pgrouting"

# The platforms the distribution workflow builds, in its own order. A release is all of them or
# nothing: a missing platform is a failed build, an extra one a workflow change to review first.
PLATFORMS = (
    "linux_amd64",
    "linux_arm64",
    "osx_amd64",
    "osx_arm64",
    "windows_amd64",
    "windows_amd64_mingw",
    "wasm_mvp",
    "wasm_eh",
    "wasm_threads",
)

_VERSION = r"v\d+\.\d+\.\d+"
_ARTIFACT_DIR = re.compile(r"^{}-({})-extension-([a-z0-9_]+)$".format(EXTENSION, _VERSION))
_ASSET = re.compile(
    r"^{}\.({})\.([a-z0-9_]+)\.duckdb_extension\.(gz|wasm)$".format(EXTENSION, _VERSION)
)
# The form DuckDB's build accepts as an extension version; anything else is reported as a hash.
_TAG = re.compile(r"^v\d+\.\d+\.\d+(-[A-Za-z0-9.]+)?$")


class ReleaseError(RuntimeError):
    """The artifacts, the tag or the built extension are not fit to publish."""


@dataclass(frozen=True)
class Artifact:
    duckdb_version: str
    platform: str
    path: pathlib.Path


def _is_wasm(platform: str) -> bool:
    return platform.startswith("wasm_")


def parse_artifact_dir(name: str) -> Tuple[str, str]:
    match = _ARTIFACT_DIR.match(name)
    if not match:
        raise ReleaseError("not a distribution artifact: {}".format(name))
    return match.group(1), match.group(2)


def asset_name(duckdb_version: str, platform: str) -> str:
    suffix = "wasm" if _is_wasm(platform) else "gz"
    return "{}.{}.{}.duckdb_extension.{}".format(EXTENSION, duckdb_version, platform, suffix)


def parse_asset_name(name: str) -> Tuple[str, str]:
    match = _ASSET.match(name)
    if not match:
        raise ReleaseError("not a release asset: {}".format(name))
    return match.group(1), match.group(2)


def check_tag(tag: str) -> None:
    if not _TAG.match(tag):
        raise ReleaseError(
            "tag {!r} is not vX.Y.Z or vX.Y.Z-suffix; DuckDB would report a commit hash".format(tag)
        )


def collect(artifacts_dir: pathlib.Path) -> List[Artifact]:
    artifacts = []
    for directory in sorted(p for p in artifacts_dir.iterdir() if p.is_dir()):
        version, platform = parse_artifact_dir(directory.name)
        file_name = EXTENSION + ".duckdb_extension" + (".wasm" if _is_wasm(platform) else "")
        path = directory / file_name
        if not path.is_file():
            raise ReleaseError("{} holds no {}".format(directory.name, file_name))
        artifacts.append(Artifact(version, platform, path))
    found = {a.platform for a in artifacts}
    missing = [p for p in PLATFORMS if p not in found]
    extra = sorted(found - set(PLATFORMS))
    if missing or extra or len(artifacts) != len(PLATFORMS):
        raise ReleaseError(
            "expected exactly one artifact per platform; missing {}, unexpected {}".format(
                missing, extra
            )
        )
    duckdb_version_of(artifacts)
    return artifacts


def duckdb_version_of(artifacts: Sequence[Artifact]) -> str:
    versions = sorted({a.duckdb_version for a in artifacts})
    if len(versions) != 1:
        raise ReleaseError("artifacts built for several DuckDB versions: {}".format(versions))
    return versions[0]


def write_assets(artifacts: Sequence[Artifact], out_dir: pathlib.Path) -> List[pathlib.Path]:
    out_dir.mkdir(parents=True, exist_ok=True)
    paths = []
    for artifact in artifacts:
        target = out_dir / asset_name(artifact.duckdb_version, artifact.platform)
        if _is_wasm(artifact.platform):
            shutil.copyfile(artifact.path, target)
        else:
            # mtime=0 and no stored file name: the same build always gives the same bytes, so the
            # published checksum depends on the binary alone.
            with target.open("wb") as raw, gzip.GzipFile(
                filename="", mode="wb", fileobj=raw, mtime=0
            ) as zipped:
                zipped.write(artifact.path.read_bytes())
        paths.append(target)
    return paths


def sha256sums(paths: Sequence[pathlib.Path]) -> str:
    lines = [
        "{}  {}".format(hashlib.sha256(p.read_bytes()).hexdigest(), p.name)
        for p in sorted(paths, key=lambda p: p.name)
    ]
    return "\n".join(lines) + "\n"


def read_pgrouting_version(cmakelists: pathlib.Path) -> str:
    match = re.search(
        r"^project\(PGROUTING VERSION (\d+\.\d+\.\d+)", cmakelists.read_text(), re.MULTILINE
    )
    if not match:
        raise ReleaseError("no 'project(PGROUTING VERSION x.y.z' line in {}".format(cmakelists))
    return match.group(1)


def verify(cli: str, extension_file: pathlib.Path, tag: str, pgrouting_version: str) -> None:
    load = "LOAD '{}';".format(str(extension_file).replace("'", "''"))
    db = duckdbcli.DuckDB(cli, preamble=load, flags=["-unsigned"])
    rows = db.query(
        "SELECT extension_version FROM duckdb_extensions() WHERE extension_name = '{}'".format(
            EXTENSION
        )
    ).rows
    if len(rows) != 1:
        raise ReleaseError("{} did not load into {}".format(extension_file, cli))
    reported = rows[0][0]
    if reported != tag:
        raise ReleaseError(
            "the build reports extension version {!r}, not the tag {!r}: the tagged commit's "
            "checkout did not produce it".format(reported, tag)
        )
    bundled = db.query("SELECT pgr_version() AS version").rows[0][0]
    if bundled != pgrouting_version:
        raise ReleaseError(
            "pgr_version() is {!r}, the pgRouting submodule is {!r}".format(bundled, pgrouting_version)
        )


def release_notes(
    tag: str, duckdb_version: str, commit: str, repository: str, pgrouting_version: str
) -> str:
    base = "https://github.com/{}/releases/download/{}/".format(repository, tag)
    rows = "\n".join(
        "| {} | `INSTALL '{}{}';` |".format(p, base, asset_name(duckdb_version, p))
        for p in PLATFORMS
        if not _is_wasm(p)
    )
    return """\
pgrouting {tag} for **DuckDB {duckdb}**, bundling pgRouting {pgr}.

An extension loads only into the DuckDB version it was built for. For another DuckDB version,
use the Release built for that version.

## Install (native)

These binaries are not signed by DuckDB. Start DuckDB with unsigned extensions allowed
(`duckdb -unsigned`, or `allow_unsigned_extensions = true` in the client's configuration) only if
you trust this project. Then run the line for your platform (`PRAGMA platform;` prints it) and
`LOAD pgrouting;`. To update an existing installation, use `FORCE INSTALL` with the same URL.

| Platform | Command |
|---|---|
{rows}

## DuckDB-Wasm

The `pgrouting.{duckdb}.wasm_*.duckdb_extension.wasm` assets are the Wasm builds. GitHub serves
Release downloads without CORS headers, so DuckDB-Wasm cannot fetch them from here: host the file
on your own origin and `LOAD '<its URL>'` with `allowUnsignedExtensions: true`.

## Source and license

GPL-2.0-or-later, because pgRouting's GPL-2.0-or-later code is statically linked. The
corresponding source is this repository at commit
[{short}](https://github.com/{repo}/tree/{commit}), including the submodules recorded there
(DuckDB, extension-ci-tools, pgRouting). `SHA256SUMS` lists every asset's checksum.

Unofficial: this extension is not affiliated with, endorsed by or sponsored by the pgRouting
project or the Open Source Geospatial Foundation.
""".format(
        tag=tag,
        duckdb=duckdb_version,
        pgr=pgrouting_version,
        rows=rows,
        short=commit[:10],
        repo=repository,
        commit=commit,
    )


def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    version = commands.add_parser("duckdb-version", help="print the artifacts' DuckDB version")
    version.add_argument("--artifacts", type=pathlib.Path, required=True)
    build = commands.add_parser("build", help="verify, then write the assets and the notes")
    build.add_argument("--artifacts", type=pathlib.Path, required=True)
    build.add_argument("--out", type=pathlib.Path, required=True)
    build.add_argument("--notes", type=pathlib.Path, required=True)
    build.add_argument("--tag", required=True)
    build.add_argument("--commit", required=True)
    build.add_argument("--repository", required=True)
    build.add_argument("--cli", required=True)
    build.add_argument("--pgrouting-cmakelists", type=pathlib.Path, required=True)
    args = parser.parse_args(argv)
    try:
        artifacts = collect(args.artifacts)
        duckdb_version = duckdb_version_of(artifacts)
        if args.command == "duckdb-version":
            print(duckdb_version)
            return 0
        check_tag(args.tag)
        pgrouting_version = read_pgrouting_version(args.pgrouting_cmakelists)
        linux = next(a for a in artifacts if a.platform == "linux_amd64")
        verify(args.cli, linux.path, args.tag, pgrouting_version)
        paths = write_assets(artifacts, args.out)
        (args.out / "SHA256SUMS").write_text(sha256sums(paths))
        args.notes.write_text(
            release_notes(args.tag, duckdb_version, args.commit, args.repository, pgrouting_version)
        )
    # OSError: a CLI path that does not exist or cannot be executed.
    except (ReleaseError, duckdbcli.DuckDBError, OSError) as exc:
        print("error: {}".format(exc), file=sys.stderr)
        return 1
    for path in paths:
        print(path.name)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
