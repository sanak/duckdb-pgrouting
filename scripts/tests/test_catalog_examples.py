# SPDX-License-Identifier: GPL-2.0-or-later
"""Every catalog example this extension registers runs on upstream's sample graph.

duckdb_functions().examples is shown on DuckDB's generated extension page, so an example that
fails there is a published defect. Each one is executed here against the release binary with the
sample tables (edges, pointsofinterest, combinations, ...) loaded. Skips when the binary is absent.
"""

import os
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import duckdbcli  # noqa: E402
import gen_docqueries_tests as gen  # noqa: E402

REPO = pathlib.Path(__file__).resolve().parents[2]
BINARY = REPO / "build/release/duckdb"


@unittest.skipUnless(BINARY.exists(), "build/release/duckdb not built")
class TestCatalogExamples(unittest.TestCase):
    def setUp(self):
        # The sample-data loader reads test/data/sampledata/*.csv by repository-relative path.
        self.addCleanup(os.chdir, os.getcwd())
        os.chdir(REPO)

    def examples(self):
        rows = duckdbcli.DuckDB(str(BINARY)).query(
            "SELECT DISTINCT function_name, unnest(examples) AS example FROM duckdb_functions() "
            "WHERE tags['ext'] = 'pgrouting' AND tags['pgrouting_name'] IS NOT NULL "
            "ORDER BY 1, 2"
        ).rows
        return [(row[0], row[1]) for row in rows]

    def test_there_are_examples_to_run(self):
        self.assertGreaterEqual(len(self.examples()), 16)

    def test_every_example_runs_and_returns_rows(self):
        db = duckdbcli.DuckDB(str(BINARY), preamble=gen._raw_preamble())
        for name, example in self.examples():
            with self.subTest(function=name):
                self.assertGreater(len(db.query(example).rows), 0, example)


if __name__ == "__main__":
    unittest.main()
