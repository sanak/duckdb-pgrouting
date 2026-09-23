# SPDX-License-Identifier: GPL-2.0-or-later
"""Unit tests for the DuckDB CLI wrapper.

These need a built binary. They skip rather than fail when one is absent, so the parser tests
still run on a machine that has not built the extension.
"""

import pathlib
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import duckdbcli  # noqa: E402

REPO = pathlib.Path(__file__).resolve().parents[2]
BINARY = REPO / "build/release/duckdb"


@unittest.skipUnless(BINARY.exists(), "build/release/duckdb not built")
class TestDuckDB(unittest.TestCase):
    def setUp(self):
        self.db = duckdbcli.DuckDB(str(BINARY))

    def test_returns_typed_values(self):
        result = self.db.query("SELECT 1::BIGINT AS a, 1.5::DOUBLE AS b, NULL::VARCHAR AS c")
        self.assertEqual(["a", "b", "c"], result.columns)
        self.assertEqual(["BIGINT", "DOUBLE", "VARCHAR"], result.types)
        self.assertEqual([[1, 1.5, None]], result.rows)

    def test_distinguishes_null_from_the_string_null(self):
        result = self.db.query("SELECT NULL::VARCHAR AS a, 'NULL' AS b")
        self.assertEqual([[None, "NULL"]], result.rows)

    def test_preamble_runs_before_every_query(self):
        db = duckdbcli.DuckDB(str(BINARY), preamble="CREATE TABLE t AS SELECT 42 AS v;")
        self.assertEqual([[42]], db.query("SELECT v FROM t").rows)

    def test_raises_on_sql_error(self):
        with self.assertRaises(duckdbcli.DuckDBError) as caught:
            self.db.query("SELECT * FROM no_such_table")
        self.assertIn("no_such_table", str(caught.exception))

    def test_the_extension_is_linked_in(self):
        rows = self.db.query(
            "SELECT count(*) AS n FROM duckdb_functions() WHERE tags['ext'] = 'pgrouting'"
        ).rows
        self.assertGreater(rows[0][0], 0)


class TimeoutTest(unittest.TestCase):
    def test_every_query_runs_with_a_timeout(self):
        completed = mock.Mock(returncode=0, stdout="[]", stderr="")
        with mock.patch("duckdbcli.subprocess.run", return_value=completed) as run:
            duckdbcli.DuckDB("duckdb")._run("SELECT 1")
        self.assertEqual(duckdbcli.TIMEOUT_SECONDS, run.call_args.kwargs["timeout"])


class FlagsTest(unittest.TestCase):
    def test_flags_go_before_batch_mode(self):
        completed = mock.Mock(returncode=0, stdout="[]", stderr="")
        with mock.patch("duckdbcli.subprocess.run", return_value=completed) as run:
            duckdbcli.DuckDB("duckdb", flags=["-unsigned"])._run("SELECT 1")
        self.assertEqual(["duckdb", "-unsigned", "-batch", ":memory:"], run.call_args.args[0])

    def test_no_flags_by_default(self):
        completed = mock.Mock(returncode=0, stdout="[]", stderr="")
        with mock.patch("duckdbcli.subprocess.run", return_value=completed) as run:
            duckdbcli.DuckDB("duckdb")._run("SELECT 1")
        self.assertEqual(["duckdb", "-batch", ":memory:"], run.call_args.args[0])


if __name__ == "__main__":
    unittest.main()
