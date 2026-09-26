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

    def test_a_dotted_sub_block_marker_is_its_own_block(self):
        # extractVertices.pg/.result is the only docqueries page that numbers sub-blocks with a
        # dot ("/* --q1.1 */"); a name segment may be joined by a hyphen or a dot.
        text = "/* --q1 */\nSELECT 1;\n/* --q1.1 */\nSELECT 2;\n/* --q2 */\nSELECT 3;\n"
        names = [b.name for b in pgparse.split_blocks(text)]
        self.assertEqual(["q1", "q1.1", "q2"], names)

    def test_a_dotted_sub_block_marker_is_recognised_in_a_result_transcript_too(self):
        # parse_result_block() itself never sees a marker: gen_docqueries_tests.process() splits a
        # .result file's text with the same split_blocks()/MARKER_RE used above, before handing
        # each block's body to parse_result_block(). This pins down that shared split on
        # .result-shaped text (a table followed by its row count).
        text = (
            "/* --q1 */\n id \n----\n  1 \n(1 row)\n"
            "/* --q1.1 */\n id \n----\n  2 \n(1 row)\n"
            "/* --q2 */\n id \n----\n  3 \n(1 row)\n"
        )
        names = [b.name for b in pgparse.split_blocks(text)]
        self.assertEqual(["q1", "q1.1", "q2"], names)


class TestExtraFloatDigits(unittest.TestCase):
    def test_reads_the_preamble_setting(self):
        text = "license header\nSET extra_float_digits=-3;\n/* -- q1 */\nSELECT 1;\n"
        self.assertEqual(-3, pgparse.extra_float_digits(text))

    def test_absent_is_none(self):
        text = "license header\n/* -- q1 */\nSELECT 1;\n"
        self.assertIsNone(pgparse.extra_float_digits(text))

    def test_tolerates_case_and_whitespace_around_equals(self):
        text = "set  EXTRA_FLOAT_DIGITS  =  -3 ;\n/* -- q1 */\nSELECT 1;\n"
        self.assertEqual(-3, pgparse.extra_float_digits(text))

    def test_a_positive_value_is_read_too(self):
        text = "SET extra_float_digits=3;\n/* -- q1 */\nSELECT 1;\n"
        self.assertEqual(3, pgparse.extra_float_digits(text))

    def test_a_statement_inside_a_named_block_does_not_count(self):
        # Only the preamble split_blocks drops is read; a SET inside a block sets its own value
        # for its own block, deliberately not the page's documented default.
        text = "/* -- q1 */\nSET extra_float_digits=-3;\nSELECT 1;\n"
        self.assertIsNone(pgparse.extra_float_digits(text))

    def test_real_withpoints_pg_sets_minus_three(self):
        text = (REPO / "third_party/pgrouting/docqueries/withPoints/withPoints.pg").read_text()
        self.assertEqual(-3, pgparse.extra_float_digits(text))

    def test_real_dijkstra_pg_has_no_setting(self):
        text = (REPO / "third_party/pgrouting/docqueries/dijkstra/dijkstra.pg").read_text()
        self.assertIsNone(pgparse.extra_float_digits(text))


class TestParseAligned(unittest.TestCase):
    # Every cell here carries only psql's own one-space margin plus alignment padding: the id and
    # cost columns are numbers (right-aligned, so their padding is extra leading spaces beyond
    # the margin), and name is text (left-aligned, so its padding is trailing).
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
        # Only the one-space margin and trailing padding are dropped here: a numeric column's own
        # right-align padding (the extra leading space before "1") survives parse_aligned and is
        # stripped the rest of the way downstream, by coerce(), which knows the column is numeric.
        self.assertEqual([[" 1", "a|b", "   1"], [" 2", "", "  -1"]], table.rows)
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

    def test_a_left_aligned_text_cell_keeps_its_own_leading_space(self):
        # A documentation query can deliberately return a string starting with a space (pgRouting's
        # withPoints.pg q7 does, building a sentence out of `status || ' ' || ...`-shaped pieces).
        # psql's own left-align padding is only ever trailing, so the single margin space is the
        # only leading whitespace parse_aligned may remove.
        lines = [
            "        status        | id ",
            "----------------------+----",
            "  visits              |   6",
            "  passes by           |  11",
            "(2 rows)",
        ]
        table, _ = pgparse.parse_aligned(lines, 0)
        self.assertEqual([[" visits", "  6"], [" passes by", " 11"]], table.rows)

    def test_a_right_aligned_number_only_loses_the_margin_here(self):
        lines = [" count", "-------", "     42", "(1 rows)"]
        table, _ = pgparse.parse_aligned(lines, 0)
        # "     42" minus the one margin space is "    42": still left-padded, exactly as a
        # right-aligned number always is; coerce() strips the rest because "I"/"R" can never have
        # a genuine leading space.
        self.assertEqual([["    42"]], table.rows)

    def test_a_blank_cell_is_still_empty_after_the_margin_is_dropped(self):
        lines = [" id | name ", "----+------", "  1 |      ", "(1 rows)"]
        table, _ = pgparse.parse_aligned(lines, 0)
        # "  1 " loses only its margin space too, same as any right-aligned number; coerce() is
        # what fully strips a numeric cell.
        self.assertEqual([[" 1", ""]], table.rows)


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
        # "  1 " loses only its one margin space, same as any right-aligned number.
        self.assertEqual([[" 1"]], block.tables[0].rows)


if __name__ == "__main__":
    unittest.main()
