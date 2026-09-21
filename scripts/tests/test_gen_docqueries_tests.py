# SPDX-License-Identifier: GPL-2.0-or-later
"""Unit tests for the docqueries test generator's pure parts."""

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import gen_docqueries_tests as gen  # noqa: E402

IMPLEMENTED = {"pgr_dijkstra": "dijkstra"}


class TestTranslate(unittest.TestCase):
    def test_strips_the_prefix_and_keeps_the_documentation_casing(self):
        sql, missing = gen.translate("SELECT * FROM pgr_Dijkstra('x', 6, 10);", IMPLEMENTED)
        self.assertEqual("SELECT * FROM Dijkstra('x', 6, 10);", sql)
        self.assertEqual([], missing)

    def test_reports_an_unimplemented_function_and_leaves_it_alone(self):
        sql, missing = gen.translate("SELECT * FROM pgr_dijkstraVia('x');", IMPLEMENTED)
        self.assertEqual("SELECT * FROM pgr_dijkstraVia('x');", sql)
        self.assertEqual(["pgr_dijkstraVia"], missing)

    def test_leaves_a_query_with_no_pgr_call_untouched(self):
        sql, missing = gen.translate("SELECT source, target FROM combinations;", IMPLEMENTED)
        self.assertEqual("SELECT source, target FROM combinations;", sql)
        self.assertEqual([], missing)

    def test_does_not_rewrite_a_name_inside_a_longer_identifier(self):
        sql, _ = gen.translate("SELECT my_pgr_dijkstra_helper(1);", IMPLEMENTED)
        self.assertEqual("SELECT my_pgr_dijkstra_helper(1);", sql)


class TestSltTypes(unittest.TestCase):
    def test_maps_duckdb_types_to_directive_characters(self):
        self.assertEqual(
            "IIIIIIRR",
            gen.slt_types(
                ["INTEGER", "INTEGER", "BIGINT", "BIGINT", "BIGINT", "BIGINT", "DOUBLE", "DOUBLE"]
            ),
        )

    def test_boolean_and_list_are_text(self):
        self.assertEqual("TT", gen.slt_types(["BOOLEAN", "BIGINT[]"]))

    def test_decimal_is_floating_point(self):
        self.assertEqual("R", gen.slt_types(["DECIMAL(10,2)"]))


class TestCoerce(unittest.TestCase):
    def test_typed_conversion_of_upstream_cells(self):
        self.assertEqual(7, gen.coerce("7", "I"))
        self.assertEqual(-1.0, gen.coerce("-1", "R"))
        self.assertEqual("abc", gen.coerce("abc", "T"))

    def test_blank_is_null(self):
        self.assertIsNone(gen.coerce("", "I"))
        self.assertIsNone(gen.coerce("", "R"))

    def test_postgres_booleans_become_python_booleans(self):
        self.assertIs(True, gen.coerce("t", "T"))
        self.assertIs(False, gen.coerce("f", "T"))


class TestRender(unittest.TestCase):
    def test_emits_a_well_formed_sqllogictest(self):
        items = [
            gen.Emitted("q2", "IIR", "SELECT * FROM Dijkstra('x', 6, 10);", [["1", "1", "0"]]),
            gen.Skipped("q99", "not implemented: pgr_dijkstraVia"),
        ]
        text = gen.render("dijkstra", "dijkstra", items)
        self.assertTrue(text.startswith("# name: test/sql/pgrouting/dijkstra/dijkstra.test\n"))
        self.assertIn("# group: [pgrouting]\n", text)
        self.assertIn("# SPDX-License-Identifier: GPL-2.0-or-later\n", text)
        self.assertIn("GENERATED FILE", text)
        self.assertIn("require routing\n", text)
        self.assertIn("# q2\nquery IIR\nSELECT * FROM Dijkstra('x', 6, 10);\n----\n1\t1\t0\n", text)
        self.assertIn("# q99: skipped - not implemented: pgr_dijkstraVia\n", text)
        self.assertTrue(text.endswith("\n"))


class TestRealDocqueryInventory(unittest.TestCase):
    def test_dijkstra_pg_blocks_are_all_translatable(self):
        repo = pathlib.Path(__file__).resolve().parents[2]
        import pgparse

        text = (repo / "third_party/pgrouting/docqueries/dijkstra/dijkstra.pg").read_text()
        blocks = pgparse.nonempty(pgparse.split_blocks(text))
        self.assertEqual(46, len(blocks))
        for block in blocks:
            _, missing = gen.translate(block.sql, IMPLEMENTED)
            self.assertEqual([], missing, "{} needs {}".format(block.name, missing))


if __name__ == "__main__":
    unittest.main()
