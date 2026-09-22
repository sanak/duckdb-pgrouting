# SPDX-License-Identifier: GPL-2.0-or-later
"""Unit tests for the docqueries test generator's pure parts."""

import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import duckdbcli  # noqa: E402
import export_sampledata  # noqa: E402
import gen_docqueries_tests as gen  # noqa: E402

# translate() only tests upstream.lower() in implemented -- a membership test on this dict's
# keys -- and derives the public spelling by stripping the "pgr_" prefix, so the value below is
# never read. This is not a second copy of the pgrouting_name mapping the repository rule forbids;
# it exists only to make that membership test true in these tests.
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


class TestCheckColumnCount(unittest.TestCase):
    def test_raises_when_upstream_has_more_columns_than_this_build(self):
        import pgparse

        table = pgparse.AlignedTable(["a", "b", "c"], [["1", "2", "3"]], 1)
        result = duckdbcli.QueryResult(["a", "b"], ["BIGINT", "BIGINT"], [[1, 2]])
        with self.assertRaises(gen.Mismatch) as ctx:
            gen.check_column_count("dijkstra", "dijkstra", "q1", table, result)
        message = str(ctx.exception)
        self.assertIn("dijkstra/dijkstra.pg q1", message)
        self.assertIn("upstream returns 3 columns", message)
        self.assertIn("this build 2", message)

    def test_raises_when_this_build_has_more_columns_than_upstream(self):
        import pgparse

        table = pgparse.AlignedTable(["a"], [["1"]], 1)
        result = duckdbcli.QueryResult(["a", "b"], ["BIGINT", "BIGINT"], [[1, 2]])
        with self.assertRaises(gen.Mismatch):
            gen.check_column_count("dijkstra", "dijkstra", "q1", table, result)

    def test_does_not_raise_when_column_counts_match(self):
        import pgparse

        table = pgparse.AlignedTable(["a"], [["1"]], 1)
        result = duckdbcli.QueryResult(["a"], ["BIGINT"], [[1]])
        gen.check_column_count("dijkstra", "dijkstra", "q1", table, result)  # no raise


class TestExpectedCells(unittest.TestCase):
    def test_boolean_cells_render_as_true_and_false(self):
        import pgparse

        table = pgparse.AlignedTable(["b"], [["t"], ["f"]], 2)
        self.assertEqual([["true"], ["false"]], gen.expected_cells(table, "T"))

    def test_blank_cell_is_still_null_not_a_boolean(self):
        import pgparse

        table = pgparse.AlignedTable(["b"], [[""]], 1)
        self.assertEqual([["NULL"]], gen.expected_cells(table, "T"))

    def test_non_boolean_text_passes_through_verbatim(self):
        import pgparse

        table = pgparse.AlignedTable(["b"], [["abc"]], 1)
        self.assertEqual([["abc"]], gen.expected_cells(table, "T"))

    def test_t_and_f_in_a_non_text_column_are_untouched(self):
        import pgparse

        table = pgparse.AlignedTable(["i"], [["7"]], 1)
        self.assertEqual([["7"]], gen.expected_cells(table, "I"))


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
        self.assertIn(export_sampledata.LOADER_SQL.strip(), text)


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


class TestTieClassification(unittest.TestCase):
    COLUMNS = ["seq", "path_seq", "start_vid", "end_vid", "node", "edge", "cost", "agg_cost"]
    DIRECTIVE = "IIIIIIRR"

    def _table(self, rows):
        import pgparse

        return pgparse.AlignedTable(self.COLUMNS, rows, len(rows))

    def _result(self, rows):
        return duckdbcli.QueryResult(
            columns=self.COLUMNS,
            types=["INTEGER", "INTEGER", "BIGINT", "BIGINT", "BIGINT", "BIGINT",
                   "DOUBLE", "DOUBLE"],
            rows=rows,
        )

    UPSTREAM = [
        ["1", "1", "6", "17", "6", "4", "1", "0"],
        ["2", "2", "6", "17", "7", "8", "1", "1"],
        ["3", "3", "6", "17", "11", "11", "1", "2"],
        ["4", "4", "6", "17", "12", "13", "1", "3"],
        ["5", "5", "6", "17", "17", "-1", "0", "4"],
    ]
    # Same endpoints, same five rows, same total cost 4; a different equal-cost middle.
    TIED = [
        [1, 1, 6, 17, 6, 4, 1.0, 0.0],
        [2, 2, 6, 17, 7, 10, 1.0, 1.0],
        [3, 3, 6, 17, 8, 12, 1.0, 2.0],
        [4, 4, 6, 17, 12, 13, 1.0, 3.0],
        [5, 5, 6, 17, 17, -1, 0.0, 4.0],
    ]

    def test_identical_rows_are_a_match(self):
        same = [[gen.coerce(c, t) for c, t in zip(r, self.DIRECTIVE)] for r in self.UPSTREAM]
        self.assertEqual(
            "match", gen.classify(self._table(self.UPSTREAM), self._result(same), self.DIRECTIVE)
        )

    def test_an_equal_cost_detour_is_a_tie(self):
        self.assertEqual(
            "tie", gen.classify(self._table(self.UPSTREAM), self._result(self.TIED),
                                self.DIRECTIVE)
        )

    def test_a_different_total_cost_is_a_defect(self):
        wrong = [list(row) for row in self.TIED]
        wrong[-1][-1] = 5.0
        self.assertEqual(
            "defect",
            gen.classify(self._table(self.UPSTREAM), self._result(wrong), self.DIRECTIVE),
        )

    def test_a_missing_path_is_a_defect(self):
        self.assertEqual(
            "defect",
            gen.classify(self._table(self.UPSTREAM), self._result(self.TIED[:-1]),
                         self.DIRECTIVE),
        )

    def test_a_result_with_no_route_cannot_be_a_tie(self):
        import pgparse

        columns = ["start_vid", "end_vid", "agg_cost"]
        table = pgparse.AlignedTable(columns, [["6", "17", "4"]], 1)
        result = duckdbcli.QueryResult(columns, ["BIGINT", "BIGINT", "DOUBLE"], [[6, 17, 5.0]])
        self.assertIsNone(gen.tie_shape(columns, result.rows, "IIR"))
        self.assertEqual("defect", gen.classify(table, result, "IIR"))

    def test_companion_wraps_the_query(self):
        self.assertEqual(
            "SELECT start_vid, end_vid, count(*), max(agg_cost)\n"
            "FROM (SELECT * FROM Dijkstra('x', 6, 10))\n"
            "GROUP BY start_vid, end_vid ORDER BY 1, 2;",
            gen.companion_sql("SELECT * FROM Dijkstra('x', 6, 10);"),
        )


class TestMergeTies(unittest.TestCase):
    OLD = {
        "dijkstra/dijkstra.pg": {"q4": {"reason": "equal-cost tie", "upstream_rows": 5, "differing_rows": 2}},
        "withPoints/withPoints.pg": {"q2": {"reason": "equal-cost tie", "upstream_rows": 7, "differing_rows": 1}},
    }

    def test_keeps_the_entries_of_a_stem_this_run_did_not_process(self):
        merged = gen.merge_ties(self.OLD, {}, {"dijkstra/dijkstra.pg"})
        self.assertEqual(self.OLD["withPoints/withPoints.pg"], merged["withPoints/withPoints.pg"])

    def test_replaces_a_processed_stem_wholesale(self):
        fresh = {"dijkstra/dijkstra.pg": {"q5": {"reason": "equal-cost tie", "upstream_rows": 3, "differing_rows": 1}}}
        merged = gen.merge_ties(self.OLD, fresh, {"dijkstra/dijkstra.pg"})
        self.assertEqual(fresh["dijkstra/dijkstra.pg"], merged["dijkstra/dijkstra.pg"])

    def test_drops_a_processed_stem_that_has_no_tie_left(self):
        merged = gen.merge_ties(self.OLD, {}, {"dijkstra/dijkstra.pg"})
        self.assertNotIn("dijkstra/dijkstra.pg", merged)


class TestSelectStems(unittest.TestCase):
    def _tree(self, files):
        root = tempfile.TemporaryDirectory()
        self.addCleanup(root.cleanup)
        for name in files:
            path = pathlib.Path(root.name) / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("")
        return pathlib.Path(root.name)

    def test_selects_the_page_of_an_implemented_function_case_insensitively(self):
        root = self._tree(["dijkstra/dijkstraCost.pg", "dijkstra/dijkstraCost.result"])
        selected = gen.select_stems(root, {"pgr_dijkstracost": "dijkstraCost"}, None)
        self.assertEqual([root / "dijkstra/dijkstraCost.pg"], selected)

    def test_skips_a_page_whose_stem_names_no_implemented_function(self):
        # contraction.pg calls pgr_dijkstra somewhere, but it is not pgr_dijkstra's own page.
        root = self._tree(["contraction/contraction.pg", "contraction/contraction.result",
                           "bdDijkstra/bdDijkstra-large.pg", "bdDijkstra/bdDijkstra-large.result"])
        self.assertEqual([], gen.select_stems(root, IMPLEMENTED, None))

    def test_skips_a_page_without_a_transcript(self):
        root = self._tree(["dijkstra/dijkstra.pg"])
        self.assertEqual([], gen.select_stems(root, IMPLEMENTED, None))

    def test_category_filter_is_repeatable(self):
        root = self._tree(["dijkstra/dijkstra.pg", "dijkstra/dijkstra.result",
                           "other/dijkstra.pg", "other/dijkstra.result"])
        self.assertEqual([root / "dijkstra/dijkstra.pg"],
                         gen.select_stems(root, IMPLEMENTED, ["dijkstra"]))
        self.assertEqual(2, len(gen.select_stems(root, IMPLEMENTED, ["dijkstra", "other"])))


class TestStaleOutputs(unittest.TestCase):
    EXISTING = ["test/sql/pgrouting/dijkstra/dijkstra.test",
                "test/sql/pgrouting/dijkstra/dijkstraVia.test",
                "test/sql/pgrouting/withPoints/withPoints.test"]

    def test_an_unscoped_run_reports_every_file_it_did_not_produce(self):
        rendered = ["test/sql/pgrouting/dijkstra/dijkstra.test"]
        self.assertEqual(["test/sql/pgrouting/dijkstra/dijkstraVia.test",
                          "test/sql/pgrouting/withPoints/withPoints.test"],
                         gen.stale_outputs(self.EXISTING, rendered, None))

    def test_a_scoped_run_only_judges_its_own_categories(self):
        rendered = ["test/sql/pgrouting/dijkstra/dijkstra.test"]
        self.assertEqual(["test/sql/pgrouting/dijkstra/dijkstraVia.test"],
                         gen.stale_outputs(self.EXISTING, rendered, ["dijkstra"]))


if __name__ == "__main__":
    unittest.main()
