# SPDX-License-Identifier: GPL-2.0-or-later
"""Unit tests for the release asset preparation."""

import gzip
import hashlib
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import duckdbcli  # noqa: E402
import release_assets  # noqa: E402

REPO = pathlib.Path(__file__).resolve().parents[2]


def make_artifacts(root, version="v1.5.5", platforms=release_assets.PLATFORMS):
    """Lay out artifacts the way actions/download-artifact does: one directory per artifact."""
    for platform in platforms:
        directory = root / "pgrouting-{}-extension-{}".format(version, platform)
        directory.mkdir(parents=True)
        name = "pgrouting.duckdb_extension" + (".wasm" if platform.startswith("wasm_") else "")
        (directory / name).write_bytes("binary for {}".format(platform).encode())


class NamingTest(unittest.TestCase):
    def test_parse_artifact_dir(self):
        self.assertEqual(
            ("v1.5.5", "windows_amd64_mingw"),
            release_assets.parse_artifact_dir("pgrouting-v1.5.5-extension-windows_amd64_mingw"),
        )

    def test_parse_artifact_dir_rejects_other_names(self):
        for name in ("routing-v1.5.5-extension-linux_amd64", "pgrouting-extension-linux_amd64"):
            with self.assertRaises(release_assets.ReleaseError):
                release_assets.parse_artifact_dir(name)

    def test_native_asset_name(self):
        self.assertEqual(
            "pgrouting.v1.5.5.osx_arm64.duckdb_extension.gz",
            release_assets.asset_name("v1.5.5", "osx_arm64"),
        )

    def test_wasm_asset_name(self):
        self.assertEqual(
            "pgrouting.v1.5.5.wasm_eh.duckdb_extension.wasm",
            release_assets.asset_name("v1.5.5", "wasm_eh"),
        )

    def test_extension_name_is_everything_before_the_first_dot(self):
        # DuckDB installs a file under the name up to its first '.', so that must be the
        # extension's own name for every asset.
        for platform in release_assets.PLATFORMS:
            name = release_assets.asset_name("v1.5.5", platform)
            self.assertEqual("pgrouting", name.split(".", 1)[0])

    def test_asset_name_round_trips(self):
        # The DuckDB version contains dots; a parser that splits on '.' gets the platform wrong.
        for platform in release_assets.PLATFORMS:
            name = release_assets.asset_name("v1.5.5", platform)
            self.assertEqual(("v1.5.5", platform), release_assets.parse_asset_name(name))


class TagTest(unittest.TestCase):
    def test_accepts_release_and_suffixed_tags(self):
        for tag in ("v0.1.0", "v1.20.3", "v0.2.0-rc1"):
            release_assets.check_tag(tag)

    def test_rejects_tags_duckdb_would_not_report(self):
        for tag in ("0.1.0", "v0.1", "v0.1.0.1", "release-0.1.0", "v0.1.0-"):
            with self.assertRaises(release_assets.ReleaseError):
                release_assets.check_tag(tag)


class CollectTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name) / "artifacts"

    def tearDown(self):
        self.tmp.cleanup()

    def test_collects_all_nine(self):
        make_artifacts(self.root)
        artifacts = release_assets.collect(self.root)
        self.assertEqual(sorted(release_assets.PLATFORMS), sorted(a.platform for a in artifacts))
        self.assertEqual("v1.5.5", release_assets.duckdb_version_of(artifacts))

    def test_collect_rejects_missing_platform(self):
        make_artifacts(self.root, platforms=release_assets.PLATFORMS[:-1])
        with self.assertRaises(release_assets.ReleaseError) as caught:
            release_assets.collect(self.root)
        self.assertIn("wasm_threads", str(caught.exception))

    def test_collect_rejects_extra_platform(self):
        make_artifacts(self.root, platforms=release_assets.PLATFORMS + ("linux_amd64_musl",))
        with self.assertRaises(release_assets.ReleaseError) as caught:
            release_assets.collect(self.root)
        self.assertIn("linux_amd64_musl", str(caught.exception))

    def test_collect_rejects_mixed_versions(self):
        make_artifacts(self.root, platforms=release_assets.PLATFORMS[:4])
        make_artifacts(self.root, version="v1.5.6", platforms=release_assets.PLATFORMS[4:])
        with self.assertRaises(release_assets.ReleaseError) as caught:
            release_assets.collect(self.root)
        self.assertIn("v1.5.6", str(caught.exception))

    def test_collect_rejects_an_artifact_without_its_file(self):
        make_artifacts(self.root)
        (self.root / "pgrouting-v1.5.5-extension-osx_arm64" / "pgrouting.duckdb_extension").unlink()
        with self.assertRaises(release_assets.ReleaseError):
            release_assets.collect(self.root)


class WriteAssetsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name)
        make_artifacts(self.root / "artifacts")
        self.artifacts = release_assets.collect(self.root / "artifacts")

    def tearDown(self):
        self.tmp.cleanup()

    def test_writes_nine_named_assets(self):
        paths = release_assets.write_assets(self.artifacts, self.root / "out")
        self.assertEqual(
            sorted(release_assets.asset_name("v1.5.5", p) for p in release_assets.PLATFORMS),
            sorted(p.name for p in paths),
        )

    def test_native_assets_are_gzip_of_the_build(self):
        paths = release_assets.write_assets(self.artifacts, self.root / "out")
        native = [p for p in paths if p.name.endswith(".gz")]
        self.assertEqual(6, len(native))
        for path in native:
            platform = release_assets.parse_asset_name(path.name)[1]
            self.assertEqual("binary for {}".format(platform).encode(), gzip.decompress(path.read_bytes()))

    def test_wasm_assets_are_copied_as_built(self):
        paths = release_assets.write_assets(self.artifacts, self.root / "out")
        wasm = [p for p in paths if p.name.endswith(".wasm")]
        self.assertEqual(3, len(wasm))
        for path in wasm:
            platform = release_assets.parse_asset_name(path.name)[1]
            self.assertEqual("binary for {}".format(platform).encode(), path.read_bytes())

    def test_gzip_output_is_reproducible(self):
        first = [p.read_bytes() for p in release_assets.write_assets(self.artifacts, self.root / "a")]
        second = [p.read_bytes() for p in release_assets.write_assets(self.artifacts, self.root / "b")]
        self.assertEqual(first, second)

    def test_sha256sums_lists_every_asset_sorted(self):
        paths = release_assets.write_assets(self.artifacts, self.root / "out")
        lines = release_assets.sha256sums(paths).splitlines()
        self.assertEqual(9, len(lines))
        names = [line.split("  ", 1)[1] for line in lines]
        self.assertEqual(sorted(names), names)
        first = sorted(paths, key=lambda p: p.name)[0]
        self.assertEqual(hashlib.sha256(first.read_bytes()).hexdigest(), lines[0].split("  ", 1)[0])


class PgroutingVersionTest(unittest.TestCase):
    def test_reads_the_submodule_version(self):
        cmakelists = REPO / "third_party/pgrouting/CMakeLists.txt"
        if not cmakelists.exists():
            self.skipTest("pgRouting submodule not checked out")
        self.assertRegex(release_assets.read_pgrouting_version(cmakelists), r"^\d+\.\d+\.\d+$")

    def test_rejects_a_file_without_the_project_line(self):
        with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as handle:
            handle.write("cmake_minimum_required(VERSION 3.12)\n")
        try:
            with self.assertRaises(release_assets.ReleaseError):
                release_assets.read_pgrouting_version(pathlib.Path(handle.name))
        finally:
            pathlib.Path(handle.name).unlink()


def result(column, value):
    return duckdbcli.QueryResult(columns=[column], types=["VARCHAR"], rows=[[value]])


class VerifyTest(unittest.TestCase):
    def test_accepts_matching_versions(self):
        with mock.patch.object(release_assets.duckdbcli, "DuckDB") as cls:
            cls.return_value.query.side_effect = [
                result("extension_version", "v0.1.0"),
                result("version", "4.0.2"),
            ]
            release_assets.verify("cli", pathlib.Path("/x/pgrouting.duckdb_extension"), "v0.1.0", "4.0.2")
        self.assertEqual(["-unsigned"], list(cls.call_args.kwargs["flags"]))
        self.assertIn("LOAD '/x/pgrouting.duckdb_extension'", cls.call_args.kwargs["preamble"])

    def test_verify_rejects_hash_version(self):
        with mock.patch.object(release_assets.duckdbcli, "DuckDB") as cls:
            cls.return_value.query.side_effect = [
                result("extension_version", "be5d9c9a1b"),
                result("version", "4.0.2"),
            ]
            with self.assertRaises(release_assets.ReleaseError) as caught:
                release_assets.verify("cli", pathlib.Path("/x/e"), "v0.1.0", "4.0.2")
        self.assertIn("be5d9c9a1b", str(caught.exception))
        self.assertIn("v0.1.0", str(caught.exception))

    def test_verify_rejects_wrong_pgrouting_version(self):
        with mock.patch.object(release_assets.duckdbcli, "DuckDB") as cls:
            cls.return_value.query.side_effect = [
                result("extension_version", "v0.1.0"),
                result("version", "3.8.0"),
            ]
            with self.assertRaises(release_assets.ReleaseError) as caught:
                release_assets.verify("cli", pathlib.Path("/x/e"), "v0.1.0", "4.0.2")
        self.assertIn("3.8.0", str(caught.exception))

    def test_verify_rejects_an_extension_that_did_not_load(self):
        with mock.patch.object(release_assets.duckdbcli, "DuckDB") as cls:
            cls.return_value.query.side_effect = [
                duckdbcli.QueryResult(columns=["extension_version"], types=["VARCHAR"], rows=[]),
            ]
            with self.assertRaises(release_assets.ReleaseError):
                release_assets.verify("cli", pathlib.Path("/x/e"), "v0.1.0", "4.0.2")

    def test_quotes_in_the_path_are_escaped(self):
        with mock.patch.object(release_assets.duckdbcli, "DuckDB") as cls:
            cls.return_value.query.side_effect = [
                result("extension_version", "v0.1.0"),
                result("version", "4.0.2"),
            ]
            release_assets.verify("cli", pathlib.Path("/it's/e"), "v0.1.0", "4.0.2")
        self.assertIn("LOAD '/it''s/e'", cls.call_args.kwargs["preamble"])


class NotesTest(unittest.TestCase):
    def setUp(self):
        self.notes = release_assets.release_notes(
            "v0.1.0", "v1.5.5", "be5d9c9" + "0" * 33, "sanak/duckdb-pgrouting", "4.0.2"
        )

    def test_has_a_tag_pinned_install_line_per_native_platform(self):
        for platform in release_assets.PLATFORMS:
            url = "https://github.com/sanak/duckdb-pgrouting/releases/download/v0.1.0/" + (
                release_assets.asset_name("v1.5.5", platform)
            )
            if platform.startswith("wasm_"):
                self.assertNotIn("INSTALL '{}'".format(url), self.notes)
            else:
                self.assertIn("INSTALL '{}';".format(url), self.notes)
        self.assertNotIn("latest/download", self.notes)

    def test_names_the_duckdb_version_source_and_license(self):
        self.assertIn("DuckDB v1.5.5", self.notes)
        self.assertIn("https://github.com/sanak/duckdb-pgrouting/tree/be5d9c9" + "0" * 33, self.notes)
        self.assertIn("GPL-2.0-or-later", self.notes)
        self.assertIn("pgRouting 4.0.2", self.notes)

    def test_carries_the_disclaimer(self):
        self.assertIn(
            "not affiliated with, endorsed by or sponsored by the pgRouting project or the Open "
            "Source Geospatial Foundation",
            " ".join(self.notes.split()),
        )


if __name__ == "__main__":
    unittest.main()
