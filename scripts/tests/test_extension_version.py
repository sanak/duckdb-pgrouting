# SPDX-License-Identifier: GPL-2.0-or-later
"""Unit tests for cmake/extension_version.cmake, run through `cmake -P` on throwaway repositories.

They skip rather than fail when cmake or git is absent.
"""

import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

REPO = pathlib.Path(__file__).resolve().parents[2]
HELPER = REPO / "cmake/extension_version.cmake"
CMAKE = shutil.which("cmake")
GIT = shutil.which("git")

GIT_ENV = dict(
    os.environ,
    GIT_AUTHOR_NAME="t",
    GIT_AUTHOR_EMAIL="t@example.org",
    GIT_COMMITTER_NAME="t",
    GIT_COMMITTER_EMAIL="t@example.org",
)


@unittest.skipUnless(CMAKE and GIT, "cmake and git are needed")
class ExtensionVersionTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name)
        self.work = self.root / "work"
        self.work.mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def git(self, *args):
        subprocess.run([GIT, *args], cwd=self.work, env=GIT_ENV, check=True, capture_output=True)

    def commit(self):
        self.git("commit", "--allow-empty", "-q", "-m", "x")

    def version(self):
        script = self.root / "print_version.cmake"
        script.write_text(
            'include("{}")\n'
            'pgrouting_extension_version(VERSION "{}")\n'
            'message("VERSION=[${{VERSION}}]")\n'.format(HELPER.as_posix(), self.work.as_posix())
        )
        completed = subprocess.run(
            [CMAKE, "-P", str(script)], capture_output=True, text=True, check=True
        )
        for line in completed.stderr.splitlines():
            if line.startswith("VERSION=["):
                return line[len("VERSION=["):-1]
        self.fail("no VERSION line in: " + completed.stderr)

    def test_an_annotated_release_tag_on_the_commit_is_the_version(self):
        self.git("init", "-q")
        self.commit()
        self.git("tag", "-a", "v0.1.0", "-m", "v0.1.0")
        self.assertEqual("v0.1.0", self.version())

    def test_a_lightweight_tag_counts_too(self):
        # actions/checkout fetches a pushed tag as a lightweight ref to the commit.
        self.git("init", "-q")
        self.commit()
        self.git("tag", "v0.1.0")
        self.assertEqual("v0.1.0", self.version())

    def test_a_suffixed_tag_is_the_version(self):
        self.git("init", "-q")
        self.commit()
        self.git("tag", "v0.2.0-rc1")
        self.assertEqual("v0.2.0-rc1", self.version())

    def test_a_commit_after_the_tag_leaves_the_version_to_duckdb(self):
        self.git("init", "-q")
        self.commit()
        self.git("tag", "v0.1.0")
        self.commit()
        self.assertEqual("", self.version())

    def test_an_untagged_commit_leaves_the_version_to_duckdb(self):
        self.git("init", "-q")
        self.commit()
        self.assertEqual("", self.version())

    def test_a_tag_of_another_shape_is_ignored(self):
        self.git("init", "-q")
        self.commit()
        self.git("tag", "release-0.1.0")
        self.assertEqual("", self.version())

    def test_outside_a_repository_the_version_is_left_to_duckdb(self):
        self.assertEqual("", self.version())


if __name__ == "__main__":
    unittest.main()
