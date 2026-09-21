# SPDX-License-Identifier: GPL-2.0-or-later
"""Unit tests for the upstream fixture parsers."""

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import pgparse  # noqa: E402

REPO = pathlib.Path(__file__).resolve().parents[2]


class TestSplitBlocks(unittest.TestCase):
    def test_tolerates_upstream_marker_spacing(self):
        text = "pre\n/* -- q1 */\nSELECT 1;\n/* -- q2*/\nSELECT 2;\n/*--q3--*/\n"
        names = [b.name for b in pgparse.split_blocks(text)]
        self.assertEqual(["q1", "q2"], names)

    def test_drops_the_preamble_and_keeps_bodies_verbatim(self):
        text = "license header\n/* -- q1 */\n  SELECT 1;\n"
        blocks = pgparse.split_blocks(text)
        self.assertEqual(1, len(blocks))
        self.assertEqual("\n  SELECT 1;\n", blocks[0].sql)

    def test_nonempty_drops_documentation_anchors(self):
        text = "/* -- q1 */\n\n\n/* -- q2 */\nSELECT 1;\n"
        self.assertEqual(["q2"], [b.name for b in pgparse.nonempty(pgparse.split_blocks(text))])

    def test_real_dijkstra_pg_has_52_markers_and_46_queries(self):
        text = (REPO / "third_party/pgrouting/docqueries/dijkstra/dijkstra.pg").read_text()
        blocks = pgparse.split_blocks(text)
        self.assertEqual(52, len(blocks))
        self.assertEqual(46, len(pgparse.nonempty(blocks)))
        self.assertIn("q154", [b.name for b in blocks])


class TestParseAligned(unittest.TestCase):
    TABLE = [
        " id | name  | cost ",
        "----+-------+------",
        "  1 | a|b   |    1 ",
        "  2 |       |   -1 ",
        "(2 rows)",
    ]

    def test_reads_columns_rows_and_count(self):
        table, nxt = pgparse.parse_aligned(self.TABLE, 0)
        self.assertEqual(["id", "name", "cost"], table.columns)
        self.assertEqual([["1", "a|b", "1"], ["2", "", "-1"]], table.rows)
        self.assertEqual(2, table.row_count)
        self.assertEqual(5, nxt)

    def test_zero_row_table(self):
        lines = [" id ", "----", "(0 rows)"]
        table, nxt = pgparse.parse_aligned(lines, 0)
        self.assertEqual([], table.rows)
        self.assertEqual(0, table.row_count)
        self.assertEqual(3, nxt)

    def test_returns_none_when_not_a_table(self):
        table, nxt = pgparse.parse_aligned(["BEGIN", "SET"], 0)
        self.assertIsNone(table)
        self.assertEqual(0, nxt)


class TestParseResultBlock(unittest.TestCase):
    def test_captures_notices_errors_and_tables(self):
        body = "\n".join(
            [
                "SELECT * FROM t;",
                "NOTICE:  No edges found",
                " id ",
                "----",
                "  1 ",
                "(1 row)",
                "ERROR:  boom",
            ]
        )
        block = pgparse.parse_result_block("q1", body)
        self.assertEqual(["No edges found"], block.notices)
        self.assertEqual("boom", block.error)
        self.assertEqual(1, len(block.tables))
        self.assertEqual([["1"]], block.tables[0].rows)


if __name__ == "__main__":
    unittest.main()
