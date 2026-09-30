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

# The lowercase upstream names translate() and select_stems() test membership against. The
# generator reads them from the pgrouting_name catalog tag; this is a test fixture, not a second
# copy of that list.
IMPLEMENTED = {"pgr_dijkstra"}


class TestTranslate(unittest.TestCase):
    def test_an_implemented_call_passes_through_verbatim(self):
        sql, missing = gen.translate("SELECT * FROM pgr_Dijkstra('x', 6, 10);", IMPLEMENTED)
        self.assertEqual("SELECT * FROM pgr_Dijkstra('x', 6, 10);", sql)
        self.assertEqual([], missing)

    def test_reports_an_unimplemented_function_and_leaves_it_alone(self):
        sql, missing = gen.translate("SELECT * FROM pgr_dijkstraVia('x');", IMPLEMENTED)
        self.assertEqual("SELECT * FROM pgr_dijkstraVia('x');", sql)
        self.assertEqual(["pgr_dijkstraVia"], missing)

    def test_leaves_a_query_with_no_pgr_call_untouched(self):
        sql, missing = gen.translate("SELECT source, target FROM combinations;", IMPLEMENTED)
        self.assertEqual("SELECT source, target FROM combinations;", sql)
        self.assertEqual([], missing)

    def test_a_longer_identifier_is_not_a_call(self):
        sql, missing = gen.translate("SELECT my_pgr_dijkstra_helper(1);", IMPLEMENTED)
        self.assertEqual("SELECT my_pgr_dijkstra_helper(1);", sql)
        self.assertEqual([], missing)


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

    def test_a_text_cells_own_leading_space_survives(self):
        # pgparse.parse_aligned only drops psql's one-space margin, keeping a genuine leading
        # space (e.g. withPoints.pg q7's ' visits'); coerce() must not strip it back off.
        self.assertEqual(" visits", gen.coerce(" visits", "T"))

    def test_a_numeric_cells_leftover_right_align_padding_is_still_stripped(self):
        # pgparse.parse_aligned leaves a right-aligned number's own padding beyond the margin
        # (e.g. "  7" for a wide column); coerce() knows the type and strips the rest of it.
        self.assertEqual(7, gen.coerce("  7", "I"))

    def test_infinity_cells_read_as_floats(self):
        self.assertEqual(float("inf"), gen.coerce("Infinity", "R"))


class TestActual(unittest.TestCase):
    def test_leaves_a_value_alone_when_no_page_setting_is_given(self):
        self.assertEqual(1.0 - 0.7, gen._actual(1.0 - 0.7, "R"))

    def test_leaves_a_value_alone_when_the_setting_is_positive(self):
        # PostgreSQL then prints shortest-exact, same as with no setting at all.
        self.assertEqual(1.0 - 0.7, gen._actual(1.0 - 0.7, "R", float_digits=3))

    def test_rounds_to_psqls_display_width_when_the_setting_is_not_positive(self):
        # float8out prints DBL_DIG (15) + extra_float_digits significant digits; at -3 that is
        # 12, which is exactly what hides the last couple of ULPs 1.0 - 0.7 leaves behind.
        self.assertEqual(0.3, gen._actual(1.0 - 0.7, "R", float_digits=-3))

    def test_rounds_at_zero_too(self):
        self.assertEqual(0.3, gen._actual(1.0 - 0.7, "R", float_digits=0))

    def test_does_not_touch_non_float_types(self):
        self.assertEqual(7, gen._actual(7, "I", float_digits=-3))
        self.assertEqual("abc", gen._actual("abc", "T", float_digits=-3))


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

    def test_a_text_cells_own_leading_space_is_emitted_verbatim(self):
        import pgparse

        table = pgparse.AlignedTable(["status"], [[" visits"]], 1)
        self.assertEqual([[" visits"]], gen.expected_cells(table, "T"))

    def test_a_numeric_cells_leftover_padding_is_stripped_on_emission_too(self):
        import pgparse

        table = pgparse.AlignedTable(["i"], [["  7"]], 1)
        self.assertEqual([["7"]], gen.expected_cells(table, "I"))

    def test_non_finite_numbers_are_written_as_duckdb_prints_them(self):
        import pgparse

        table = pgparse.AlignedTable(["r"], [["Infinity"], ["-Infinity"], ["NaN"], ["1.5"]], 4)
        self.assertEqual([["inf"], ["-inf"], ["nan"], ["1.5"]], gen.expected_cells(table, "R"))

    def test_infinity_in_a_text_column_is_untouched(self):
        import pgparse

        table = pgparse.AlignedTable(["s"], [["Infinity"]], 1)
        self.assertEqual([["Infinity"]], gen.expected_cells(table, "T"))


class TestListCells(unittest.TestCase):
    def test_an_integer_array_cell_is_respelled_as_a_duckdb_list(self):
        self.assertEqual("[12, 17, 16]", gen.pg_array_cell("{12,17,16}"))
        self.assertEqual("[-1, 2]", gen.pg_array_cell("{-1,2}"))
        self.assertEqual("[]", gen.pg_array_cell("{}"))

    def test_anything_else_is_left_alone(self):
        self.assertEqual("abc", gen.pg_array_cell("abc"))
        self.assertEqual("POINT (1 2)", gen.pg_array_cell("POINT (1 2)"))

    def test_only_integer_list_columns_are_respelled(self):
        import pgparse

        table = pgparse.AlignedTable(["node", "targets"], [["6", "{}"], ["8", "{12,17,16}"]], 2)
        respelled = gen.respell_list_cells(table, ["BIGINT", "BIGINT[]"])
        self.assertEqual([["6", "[]"], ["8", "[12, 17, 16]"]], respelled.rows)
        self.assertEqual(table.rows, gen.respell_list_cells(table, ["BIGINT", "VARCHAR"]).rows)
        self.assertEqual(table.rows, gen.respell_list_cells(table, ["BIGINT", "VARCHAR[]"]).rows)

    def test_a_blank_list_cell_stays_null(self):
        import pgparse

        table = pgparse.AlignedTable(["targets"], [[""]], 1)
        self.assertEqual([[""]], gen.respell_list_cells(table, ["BIGINT[]"]).rows)

    def test_a_list_column_compares_equal_and_emits_duckdbs_spelling(self):
        import duckdbcli
        import pgparse

        table = gen.respell_list_cells(
            pgparse.AlignedTable(["node", "targets"], [["6", "{}"], ["8", "{12,17,16}"]], 2),
            ["BIGINT", "BIGINT[]"])
        result = duckdbcli.QueryResult(columns=["node", "targets"], types=["BIGINT", "BIGINT[]"],
                                       rows=[[6, []], [8, [12, 17, 16]]])
        self.assertEqual("match", gen.classify(table, result, "IT"))
        self.assertEqual([["6", "[]"], ["8", "[12, 17, 16]"]], gen.expected_cells(table, "IT"))


class TestRender(unittest.TestCase):
    def test_emits_a_well_formed_sqllogictest(self):
        items = [
            gen.Emitted("q2", "IIR", "SELECT * FROM pgr_Dijkstra('x', 6, 10);", [["1", "1", "0"]]),
            gen.Skipped("q99", "not implemented: pgr_dijkstraVia"),
        ]
        text = gen.render("dijkstra", "dijkstra", items)
        self.assertTrue(text.startswith("# name: test/sql/pgrouting/dijkstra/dijkstra.test\n"))
        self.assertIn("# group: [pgrouting]\n", text)
        self.assertIn("# SPDX-License-Identifier: GPL-2.0-or-later\n", text)
        self.assertIn("GENERATED FILE", text)
        self.assertIn("require pgrouting\n", text)
        self.assertIn("# q2\nquery IIR\nSELECT * FROM pgr_Dijkstra('x', 6, 10);\n----\n1\t1\t0\n", text)
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

    # The exact values withPoints.pg q1 disagreed on: upstream's committed transcript (captured
    # under `SET extra_float_digits=-3;`) against this build's raw double for `1.0 - 0.7`.
    FP_NOISE_UPSTREAM = [["1", "1", "-1", "10", "-6", "4", "0.3", "2.1"]]
    FP_NOISE_OURS = [[1, 1, -1, 10, -6, 4, 0.30000000000000004, 2.0999999999999996]]

    def test_fp_noise_is_a_defect_without_the_pages_extra_float_digits(self):
        self.assertEqual(
            "defect",
            gen.classify(self._table(self.FP_NOISE_UPSTREAM), self._result(self.FP_NOISE_OURS),
                        self.DIRECTIVE),
        )

    def test_the_same_fp_noise_is_a_match_under_the_pages_extra_float_digits(self):
        self.assertEqual(
            "match",
            gen.classify(self._table(self.FP_NOISE_UPSTREAM), self._result(self.FP_NOISE_OURS),
                        self.DIRECTIVE, float_digits=-3),
        )

    def test_companion_wraps_the_query(self):
        self.assertEqual(
            "SELECT start_vid, end_vid, count(*), max(agg_cost)\n"
            "FROM (SELECT * FROM Dijkstra('x', 6, 10))\n"
            "GROUP BY start_vid, end_vid ORDER BY 1, 2;",
            gen.companion_sql("SELECT * FROM Dijkstra('x', 6, 10);"),
        )


class TestTreeTieClassification(unittest.TestCase):
    COLUMNS = ["seq", "depth", "start_vid", "pred", "node", "edge", "cost", "agg_cost"]
    DIRECTIVE = "IIIIIIRR"
    SQL = "SELECT * FROM pgr_drivingDistance('SELECT id, source, target, cost FROM edges', 11, 3.0)"

    UPSTREAM = [
        ["1", "0", "11", "11", "11", "-1", "0", "0"],
        ["2", "1", "11", "11", "12", "11", "1", "1"],
        ["3", "1", "11", "11", "16", "9", "1", "1"],
        ["4", "2", "11", "16", "17", "15", "1", "2"],
    ]
    # 17 reached from 12 instead of 16: same vertices, same costs.
    TIED = [
        [1, 0, 11, 11, 11, -1, 0.0, 0.0],
        [2, 1, 11, 11, 12, 11, 1.0, 1.0],
        [3, 1, 11, 11, 16, 9, 1.0, 1.0],
        [4, 2, 11, 12, 17, 13, 1.0, 2.0],
    ]

    def _table(self, rows):
        import pgparse

        return pgparse.AlignedTable(self.COLUMNS, rows, len(rows))

    def _result(self, rows):
        return duckdbcli.QueryResult(columns=self.COLUMNS, types=["BIGINT"] * 6 + ["DOUBLE"] * 2, rows=rows)

    def test_another_equal_cost_predecessor_is_a_tree_tie(self):
        self.assertEqual("tree_tie", gen.classify(self._table(self.UPSTREAM), self._result(self.TIED),
                                                  self.DIRECTIVE, None, self.SQL))

    def test_a_different_cost_is_a_defect(self):
        wrong = [list(row) for row in self.TIED]
        wrong[-1][-1] = 3.0
        self.assertEqual("defect", gen.classify(self._table(self.UPSTREAM), self._result(wrong),
                                                self.DIRECTIVE, None, self.SQL))

    def test_a_spanning_tree_has_no_tree_invariant(self):
        sql = self.SQL.replace("pgr_drivingDistance", "pgr_kruskalDD")
        self.assertEqual("defect", gen.classify(self._table(self.UPSTREAM), self._result(self.TIED),
                                                self.DIRECTIVE, None, sql))

    def test_the_companion_expects_upstreams_vertices_and_costs(self):
        self.assertEqual([["11", "11", "0"], ["11", "12", "1"], ["11", "16", "1"], ["11", "17", "2"]],
                         gen.tree_companion_rows(self._table(self.UPSTREAM), self.DIRECTIVE, self.SQL))


class TestKspTieClassification(unittest.TestCase):
    COLUMNS = ["seq", "path_id", "path_seq", "start_vid", "end_vid", "node", "edge", "cost", "agg_cost"]
    DIRECTIVE = "IIIIIIIRR"
    SQL = "SELECT * FROM pgr_KSP('SELECT id, source, target, cost, reverse_cost FROM edges', 6, 17, 2)"

    # Upstream's q1: 6 -> 17 through 8 and 12, then through 11 and 16.
    UPSTREAM = [
        ["1", "1", "1", "6", "17", "6", "4", "1", "0"],
        ["2", "1", "2", "6", "17", "7", "10", "1", "1"],
        ["3", "1", "3", "6", "17", "8", "12", "1", "2"],
        ["4", "1", "4", "6", "17", "12", "13", "1", "3"],
        ["5", "1", "5", "6", "17", "17", "-1", "0", "4"],
        ["6", "2", "1", "6", "17", "6", "4", "1", "0"],
        ["7", "2", "2", "6", "17", "7", "8", "1", "1"],
        ["8", "2", "3", "6", "17", "11", "9", "1", "2"],
        ["9", "2", "4", "6", "17", "16", "15", "1", "3"],
        ["10", "2", "5", "6", "17", "17", "-1", "0", "4"],
    ]
    # The third route of cost 4 (through 11 and 12) in place of upstream's second: the same
    # endpoints, row count and maximum cost, but a different answer to "which K paths".
    OTHER = [
        [1, 1, 1, 6, 17, 6, 4, 1.0, 0.0],
        [2, 1, 2, 6, 17, 7, 10, 1.0, 1.0],
        [3, 1, 3, 6, 17, 8, 12, 1.0, 2.0],
        [4, 1, 4, 6, 17, 12, 13, 1.0, 3.0],
        [5, 1, 5, 6, 17, 17, -1, 0.0, 4.0],
        [6, 2, 1, 6, 17, 6, 4, 1.0, 0.0],
        [7, 2, 2, 6, 17, 7, 8, 1.0, 1.0],
        [8, 2, 3, 6, 17, 11, 11, 1.0, 2.0],
        [9, 2, 4, 6, 17, 12, 13, 1.0, 3.0],
        [10, 2, 5, 6, 17, 17, -1, 0.0, 4.0],
    ]

    def _table(self, rows):
        import pgparse

        return pgparse.AlignedTable(self.COLUMNS, rows, len(rows))

    def _result(self, rows):
        return duckdbcli.QueryResult(
            columns=self.COLUMNS, types=["INTEGER"] * 3 + ["BIGINT"] * 4 + ["DOUBLE"] * 2, rows=rows)

    def test_another_set_of_equal_cost_paths_is_a_defect(self):
        self.assertEqual("defect", gen.classify(self._table(self.UPSTREAM), self._result(self.OTHER),
                                                self.DIRECTIVE, None, self.SQL))

    def test_the_same_rows_still_match(self):
        same = [[gen.coerce(c, t) for c, t in zip(r, self.DIRECTIVE)] for r in self.UPSTREAM]
        self.assertEqual("match", gen.classify(self._table(self.UPSTREAM), self._result(same),
                                               self.DIRECTIVE, None, self.SQL))

    def test_withpointsksp_is_guarded_too(self):
        sql = self.SQL.replace("pgr_KSP", "pgr_withPointsKSP")
        self.assertEqual("defect", gen.classify(self._table(self.UPSTREAM), self._result(self.OTHER),
                                                self.DIRECTIVE, None, sql))

    def test_other_path_functions_keep_the_route_invariant(self):
        sql = self.SQL.replace("pgr_KSP", "pgr_dijkstraVia")
        self.assertEqual("tie", gen.classify(self._table(self.UPSTREAM), self._result(self.OTHER),
                                             self.DIRECTIVE, None, sql))

    def test_turnrestrictedpath_is_guarded_too(self):
        sql = self.SQL.replace("pgr_KSP", "pgr_turnRestrictedPath")
        self.assertEqual("defect", gen.classify(self._table(self.UPSTREAM), self._result(self.OTHER),
                                                self.DIRECTIVE, None, sql))


class TestForestCompanion(unittest.TestCase):
    def _table(self, columns, rows):
        import pgparse

        return pgparse.AlignedTable(columns, rows, len(rows))

    # --- edge, cost shape (pgr_kruskal, pgr_prim) -----------------------------------------

    EDGE_COLUMNS = ["edge", "cost"]
    EDGE_DIRECTIVE = "IR"
    EDGE_UPSTREAM = [["1", "1"], ["2", "1"], ["3", "1"]]

    def test_edge_cost_shape_is_recognised(self):
        self.assertEqual("edges", gen.forest_shape(self.EDGE_COLUMNS))

    def test_edge_cost_companion_counts_rows_and_sums_cost(self):
        sql = gen.forest_companion_sql("SELECT * FROM pgr_kruskal('x')", "edges")
        self.assertIn("count(*), coalesce(sum(cost), 0)", sql)
        table = self._table(self.EDGE_COLUMNS, self.EDGE_UPSTREAM)
        self.assertEqual(
            [["3", "3"]], gen.forest_companion_rows(table, self.EDGE_DIRECTIVE, "edges")
        )

    def test_an_empty_forest_expects_a_zero_sum_not_null(self):
        # No edges at all: upstream's row count is 0 and there is nothing to sum. The companion
        # SQL's coalesce(sum(cost), 0) keeps this build's actual answer from coming back NULL for
        # the same empty case, so the two sides can still be compared as plain text.
        table = self._table(self.EDGE_COLUMNS, [])
        self.assertEqual([["0", "0"]], gen.forest_companion_rows(table, self.EDGE_DIRECTIVE, "edges"))

    # --- tree shape (pgr_kruskalBFS/DFS/DD, pgr_primBFS/DFS/DD) ---------------------------

    TREE_COLUMNS = ["seq", "depth", "start_vid", "pred", "node", "edge", "cost", "agg_cost"]
    TREE_DIRECTIVE = "IIIIIIRR"

    # Two clean roots, 6 and 9: each has a single depth-0 row, no repeated node, and every
    # other row hangs off its predecessor one level up at the predecessor's cost plus its own.
    TREE_UPSTREAM = [
        ["1", "0", "6", "6", "6", "-1", "0", "0"],
        ["2", "1", "6", "6", "5", "1", "1", "1"],
        ["3", "2", "6", "5", "15", "3", "1", "2"],
        ["4", "0", "9", "9", "9", "-1", "0", "0"],
        ["5", "1", "9", "9", "20", "5", "1", "1"],
    ]

    def test_tree_shape_is_recognised(self):
        self.assertEqual("tree", gen.forest_shape(self.TREE_COLUMNS))

    def test_tree_companion_sql_asserts_the_invariant(self):
        sql = gen.forest_companion_sql("SELECT * FROM pgr_kruskalBFS('x', 6)", "tree")
        self.assertIn("count(*) FILTER (WHERE depth = 0)", sql)
        self.assertIn("count(*) = count(DISTINCT node)", sql)
        self.assertIn("NOT EXISTS", sql)

    def test_clean_tree_has_no_orphans_and_no_repeats(self):
        table = self._table(self.TREE_COLUMNS, self.TREE_UPSTREAM)
        self.assertEqual(
            [["6", "1", "true", "0"], ["9", "1", "true", "0"]],
            gen.forest_companion_rows(table, self.TREE_DIRECTIVE, "tree"),
        )

    def test_a_row_whose_predecessor_is_unreachable_is_an_orphan(self):
        # Row 3's pred (99) names no row of root 6 one level up: an orphan, count 1.
        orphaned = [list(row) for row in self.TREE_UPSTREAM]
        orphaned[2] = ["3", "2", "6", "99", "15", "3", "1", "2"]
        table = self._table(self.TREE_COLUMNS, orphaned)
        self.assertEqual(
            [["6", "1", "true", "1"], ["9", "1", "true", "0"]],
            gen.forest_companion_rows(table, self.TREE_DIRECTIVE, "tree"),
        )

    def test_a_repeated_node_under_one_root_is_false(self):
        # Row 3's node (5) repeats row 2's node under the same root 6; its predecessor link
        # still holds, so only the no-repeat flag flips, not the orphan count.
        repeated = [list(row) for row in self.TREE_UPSTREAM]
        repeated[2] = ["3", "2", "6", "5", "5", "3", "1", "2"]
        table = self._table(self.TREE_COLUMNS, repeated)
        self.assertEqual(
            [["6", "1", "false", "0"], ["9", "1", "true", "0"]],
            gen.forest_companion_rows(table, self.TREE_DIRECTIVE, "tree"),
        )

    # --- unrecognised shape ----------------------------------------------------------------

    def test_an_unrecognised_column_set_is_no_shape(self):
        self.assertIsNone(gen.forest_shape(["node", "edge"]))

    # --- the forest-call decision -----------------------------------------------------------

    def test_kruskal_and_prim_calls_are_forest_calls(self):
        self.assertTrue(gen.is_forest_call("SELECT * FROM pgr_kruskalDD('x', 6, 3.0)"))
        self.assertTrue(gen.is_forest_call("SELECT * FROM PGR_PRIM('x')"))

    def test_driving_distance_and_dijkstra_are_not_forest_calls(self):
        self.assertFalse(gen.is_forest_call("SELECT * FROM pgr_drivingDistance('x', 6, 3.0)"))
        self.assertFalse(gen.is_forest_call("SELECT * FROM pgr_dijkstra('x', 6, 10)"))

    # --- the argument-count helper -----------------------------------------------------------

    def test_two_args_is_two(self):
        self.assertEqual(
            2,
            gen.top_level_arg_count(
                "'SELECT id, source, target, cost, reverse_cost FROM edges ORDER BY id', 6"
            ),
        )

    def test_an_array_literal_and_a_named_max_depth_is_three(self):
        self.assertEqual(
            3,
            gen.top_level_arg_count(
                "'SELECT id, source, target, cost FROM edges', ARRAY[9, 6], max_depth => 3"
            ),
        )

    def test_commas_and_parens_inside_a_quoted_sql_argument_do_not_count(self):
        # The inner query itself calls a function and uses an IN-list: neither's commas or
        # parentheses are a top-level separator of the outer call's own two arguments.
        self.assertEqual(
            2,
            gen.top_level_arg_count(
                "'SELECT id, foo(a, b), source, target FROM edges WHERE id IN (1, 2, 3)', 6"
            ),
        )

    # --- unlimited vs. limited *BFS/*DFS, and the DD forms ------------------------------------

    def test_bfs_with_only_the_two_required_arguments_is_unlimited(self):
        self.assertTrue(
            gen.is_unlimited_forest_call(
                "SELECT * FROM pgr_kruskalBFS(\n"
                "  'SELECT id, source, target, cost, reverse_cost FROM edges ORDER BY id',\n"
                "  6)"
            )
        )

    def test_bfs_with_a_named_max_depth_is_not_unlimited(self):
        self.assertFalse(
            gen.is_unlimited_forest_call(
                "SELECT * FROM pgr_kruskalBFS(\n"
                "  'SELECT id, source, target, cost, reverse_cost FROM edges ORDER BY id',\n"
                "  ARRAY[9, 6], max_depth => 3)"
            )
        )

    def test_a_dd_call_is_never_unlimited(self):
        self.assertFalse(gen.is_unlimited_forest_call("SELECT * FROM pgr_kruskalDD('x', 6, 3.0)"))

    def test_forest_variant_picks_tree_unlimited_only_for_the_unlimited_bfs_dfs_call(self):
        unlimited_sql = "SELECT * FROM pgr_primDFS('x', 6)"
        limited_sql = "SELECT * FROM pgr_primDFS('x', 6, max_depth => 2)"
        dd_sql = "SELECT * FROM pgr_kruskalDD('x', 6, 3.0)"
        self.assertEqual("tree_unlimited", gen.forest_variant("tree", unlimited_sql))
        self.assertEqual("tree", gen.forest_variant("tree", limited_sql))
        self.assertEqual("tree", gen.forest_variant("tree", dd_sql))
        self.assertEqual("edges", gen.forest_variant("edges", unlimited_sql))

    # --- the five-column companion for an unlimited walk --------------------------------------

    def test_tree_unlimited_sql_adds_a_row_count_column(self):
        limited_sql = gen.forest_companion_sql("SELECT * FROM pgr_kruskalBFS('x', 6)", "tree")
        unlimited_sql = gen.forest_companion_sql(
            "SELECT * FROM pgr_kruskalBFS('x', 6)", "tree_unlimited"
        )
        self.assertEqual(3, limited_sql.count("count(*)"))
        self.assertEqual(4, unlimited_sql.count("count(*)"))
        self.assertIn("1e-9)),\n       count(*)\nFROM q", unlimited_sql)

    def test_tree_unlimited_expects_each_roots_total_row_count(self):
        table = self._table(self.TREE_COLUMNS, self.TREE_UPSTREAM)
        self.assertEqual(
            [["6", "1", "true", "0", "3"], ["9", "1", "true", "0", "2"]],
            gen.forest_companion_rows(table, self.TREE_DIRECTIVE, "tree_unlimited"),
        )


class TestOrderingCompanion(unittest.TestCase):
    def _table(self, columns, rows):
        import pgparse

        return pgparse.AlignedTable(columns, rows, len(rows))

    def test_the_three_bandwidth_orderings_are_ordering_calls(self):
        for name in ("pgr_cuthillMckeeOrdering", "pgr_kingOrdering", "pgr_sloanOrdering"):
            with self.subTest(name=name):
                self.assertTrue(gen.is_ordering_call("SELECT * FROM {}('SELECT 1')".format(name)))

    def test_topological_sort_and_kruskal_are_not_ordering_calls(self):
        self.assertFalse(gen.is_ordering_call("SELECT * FROM pgr_topologicalSort('SELECT 1')"))
        self.assertFalse(gen.is_ordering_call("SELECT * FROM pgr_kruskal('SELECT 1')"))

    def test_companion_sql_counts_rows_vertices_and_seq_bounds(self):
        self.assertEqual(
            "SELECT count(*), count(DISTINCT node), min(seq), max(seq)\n"
            "FROM (SELECT * FROM pgr_kingOrdering('x'));",
            gen.ordering_companion_sql("SELECT * FROM pgr_kingOrdering('x');\n"),
        )

    def test_companion_rows_come_from_upstreams_table(self):
        # A Sloan-like answer that repeats vertex 1: four rows, two vertices, seq 1 to 4.
        table = self._table(["seq", "node"], [[" 1", "1"], [" 2", "2"], [" 3", "1"], [" 4", "1"]])
        self.assertEqual([["4", "2", "1", "4"]], gen.ordering_companion_rows(table, "II"))

    def test_an_empty_ordering_expects_null_bounds(self):
        self.assertEqual([["0", "0", "NULL", "NULL"]],
                         gen.ordering_companion_rows(self._table(["seq", "node"], []), "II"))

    def test_actual_rows_render_like_the_expected_ones(self):
        columns = ["count_star()", "count(DISTINCT node)", "min(seq)", "max(seq)"]
        full = duckdbcli.QueryResult(columns, ["BIGINT"] * 4, [[17, 13, 1, 17]])
        self.assertEqual([["17", "13", "1", "17"]], gen.ordering_actual_rows(full))
        empty = duckdbcli.QueryResult(columns, ["BIGINT"] * 4, [[0, 0, None, None]])
        self.assertEqual([["0", "0", "NULL", "NULL"]], gen.ordering_actual_rows(empty))


class TestClassifyTextLeadingSpace(unittest.TestCase):
    def test_a_text_cells_leading_space_is_a_match_not_a_defect(self):
        import pgparse

        # withPoints.pg q7's regression: a documentation query deliberately returns ' visits',
        # and this build's raw DuckDB text for it must classify as identical to upstream's.
        table = pgparse.AlignedTable(["status"], [[" visits"]], 1)
        result = duckdbcli.QueryResult(["status"], ["VARCHAR"], [[" visits"]])
        self.assertEqual("match", gen.classify(table, result, "T"))


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
        selected = gen.select_stems(root, {"pgr_dijkstracost"}, None)
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


class TestDuckdbWkt(unittest.TestCase):
    def test_point_gets_duckdbs_space_before_the_parenthesis(self):
        self.assertEqual("POINT (0 1.5)", gen.duckdb_wkt("POINT(0 1.5)"))

    def test_every_comma_gets_a_following_space(self):
        self.assertEqual("LINESTRING (1.8 0.4, 2 0.4)", gen.duckdb_wkt("LINESTRING(1.8 0.4,2 0.4)"))
        self.assertEqual("POLYGON ((0 0, 1 0, 0 1, 0 0))", gen.duckdb_wkt("POLYGON((0 0,1 0,0 1,0 0))"))

    def test_duckdb_spelling_is_left_alone(self):
        self.assertEqual("POINT (1 2)", gen.duckdb_wkt("POINT (1 2)"))

    def test_other_text_is_left_alone(self):
        self.assertEqual("visits(1,2)", gen.duckdb_wkt("visits(1,2)"))
        self.assertEqual("r", gen.duckdb_wkt("r"))

    def test_coerce_and_expected_cells_respell_wkt_in_text_columns_only(self):
        import pgparse

        self.assertEqual("POINT (0 1.5)", gen.coerce("POINT(0 1.5)", "T"))
        table = pgparse.AlignedTable(["geom"], [["POINT(0 1.5)"]], 1)
        self.assertEqual([["POINT (0 1.5)"]], gen.expected_cells(table, "T"))


class TestSpatialRender(unittest.TestCase):
    def test_a_spatial_page_is_tagged_and_loads_spatial_before_the_sample_data(self):
        items = [gen.Emitted("o0", "I", "SELECT 1;", [["1"]])]
        text = gen.render("utilities", "findCloseEdges", items, spatial=True)
        tags = text.index("tags spatial\n")
        require = text.index("require pgrouting\n")
        install = text.index("statement ok\nINSTALL spatial;\n")
        load = text.index("statement ok\nLOAD spatial;\n")
        loader = text.index(export_sampledata.LOADER_SQL.strip())
        self.assertLess(tags, require)
        self.assertLess(require, install)
        self.assertLess(install, load)
        self.assertLess(load, loader)

    def test_a_plain_page_is_unchanged(self):
        items = [gen.Emitted("q1", "I", "SELECT 1;", [["1"]])]
        text = gen.render("dijkstra", "dijkstra", items)
        self.assertNotIn("spatial", text)
        self.assertIn("\n\nrequire pgrouting\n\n" + export_sampledata.LOADER_SQL.strip(), text)


class TestStaleOutputsKeepsUnverifiedSpatialPages(unittest.TestCase):
    def test_a_kept_page_is_not_stale(self):
        existing = ["test/sql/pgrouting/utilities/findCloseEdges.test",
                    "test/sql/pgrouting/dijkstra/dijkstra.test"]
        rendered = ["test/sql/pgrouting/dijkstra/dijkstra.test"]
        kept = {"test/sql/pgrouting/utilities/findCloseEdges.test"}
        self.assertEqual([], gen.stale_outputs(existing, rendered, None, kept))

    def test_without_kept_pages_it_is_stale_as_before(self):
        existing = ["test/sql/pgrouting/utilities/findCloseEdges.test"]
        self.assertEqual(existing, gen.stale_outputs(existing, [], None))


class TestFlowTieClassification(unittest.TestCase):
    COLUMNS = ["seq", "edge", "start_vid", "end_vid", "flow", "residual_capacity"]
    DIRECTIVE = "IIIIII"
    SQL = "SELECT * FROM pgr_edmondsKarp('SELECT id, source, target, capacity FROM edges', 1, 4)"

    # One unit from 1 to 4, through 2.
    UPSTREAM = [["1", "1", "1", "2", "1", "0"], ["2", "2", "2", "4", "1", "0"]]
    # The same unit through 3: another maximum flow of the same value.
    OTHER = [[1, 3, 1, 3, 1, 0], [2, 4, 3, 4, 1, 0]]
    # Two units: not a maximum flow of upstream's value.
    MORE = [[1, 1, 1, 2, 1, 0], [2, 2, 2, 4, 1, 0], [3, 3, 1, 3, 1, 0], [4, 4, 3, 4, 1, 0]]

    def _table(self, columns, rows):
        import pgparse

        return pgparse.AlignedTable(columns, rows, len(rows))

    def _result(self, columns, rows, types):
        return duckdbcli.QueryResult(columns=columns, types=types, rows=rows)

    def test_another_flow_of_the_same_value_is_a_flow_tie(self):
        verdict = gen.classify(self._table(self.COLUMNS, self.UPSTREAM),
                               self._result(self.COLUMNS, self.OTHER, ["INTEGER"] + ["BIGINT"] * 5),
                               self.DIRECTIVE, None, self.SQL)
        self.assertEqual("flow_tie", verdict)

    def test_a_different_value_is_a_defect(self):
        verdict = gen.classify(self._table(self.COLUMNS, self.UPSTREAM),
                               self._result(self.COLUMNS, self.MORE, ["INTEGER"] + ["BIGINT"] * 5),
                               self.DIRECTIVE, None, self.SQL)
        self.assertEqual("defect", verdict)

    def test_the_same_rows_still_match(self):
        same = [[gen.coerce(c, t) for c, t in zip(r, self.DIRECTIVE)] for r in self.UPSTREAM]
        verdict = gen.classify(self._table(self.COLUMNS, self.UPSTREAM),
                               self._result(self.COLUMNS, same, ["INTEGER"] + ["BIGINT"] * 5),
                               self.DIRECTIVE, None, self.SQL)
        self.assertEqual("match", verdict)

    def test_the_total_is_the_sources_net_outflow(self):
        # 1 -> 2 -> 4 plus a cycle 2 -> 3 -> 2 that adds no value.
        rows = [[1, 1, 1, 2, 1, 0], [2, 2, 2, 4, 1, 0], [3, 5, 2, 3, 1, 0], [4, 6, 3, 2, 1, 0]]
        self.assertEqual((1, None), gen.flow_shape(self.COLUMNS, rows, self.DIRECTIVE))

    def test_a_min_cost_flow_compares_its_total_cost_too(self):
        columns = ["seq", "edge", "source", "target", "flow", "residual_capacity", "cost", "agg_cost"]
        directive = "IIIIIIRR"
        sql = self.SQL.replace("pgr_edmondsKarp", "pgr_maxFlowMinCost")
        upstream = [["1", "1", "1", "2", "1", "0", "1", "1"], ["2", "2", "2", "4", "1", "0", "1", "2"]]
        dearer = [[1, 3, 1, 3, 1, 0, 5.0, 5.0], [2, 4, 3, 4, 1, 0, 5.0, 10.0]]
        types = ["INTEGER"] + ["BIGINT"] * 5 + ["DOUBLE"] * 2
        self.assertEqual("defect", gen.classify(self._table(columns, upstream),
                                                self._result(columns, dearer, types), directive, None, sql))
        self.assertEqual((1, 2.0), gen.flow_shape(columns, upstream, directive))

    def test_other_functions_never_take_the_flow_invariant(self):
        sql = self.SQL.replace("pgr_edmondsKarp", "pgr_dijkstra")
        verdict = gen.classify(self._table(self.COLUMNS, self.UPSTREAM),
                               self._result(self.COLUMNS, self.OTHER, ["INTEGER"] + ["BIGINT"] * 5),
                               self.DIRECTIVE, None, sql)
        self.assertEqual("defect", verdict)

    def test_the_companion_asserts_the_total(self):
        companion = gen.flow_companion_sql(self.SQL + ";", self.COLUMNS)
        self.assertIn("WITH r AS (" + self.SQL + ")", companion)
        self.assertIn("SELECT start_vid AS v, flow AS f FROM r", companion)
        self.assertIn("SELECT end_vid, -flow FROM r", companion)
        self.assertNotIn("sum(cost)", companion)
        self.assertEqual("I", gen.flow_directive(self.COLUMNS))
        self.assertEqual([["1"]], gen.flow_companion_rows(self._table(self.COLUMNS, self.UPSTREAM), self.DIRECTIVE))

    def test_the_min_cost_companion_asserts_the_total_cost_too(self):
        columns = ["seq", "edge", "source", "target", "flow", "residual_capacity", "cost", "agg_cost"]
        upstream = [["1", "1", "1", "2", "1", "0", "1", "1"], ["2", "2", "2", "4", "1", "0", "1.5", "2.5"]]
        companion = gen.flow_companion_sql(self.SQL, columns)
        self.assertIn("SELECT source AS v, flow AS f FROM r", companion)
        self.assertIn("(SELECT sum(cost) FROM r)", companion)
        self.assertEqual("IR", gen.flow_directive(columns))
        self.assertEqual([["1", "2.5"]], gen.flow_companion_rows(self._table(columns, upstream), "IIIIIIRR"))

    def test_a_zero_value_circulation_is_asserted_as_zero(self):
        loop_a = [["1", "1", "1", "2", "1", "0"], ["2", "1", "2", "1", "1", "0"]]
        loop_b = [[1, 2, 3, 4, 1, 0], [2, 2, 4, 3, 1, 0]]
        self.assertIn("coalesce(sum(o), 0)", gen.flow_companion_sql(self.SQL, self.COLUMNS))
        self.assertEqual([["0"]], gen.flow_companion_rows(self._table(self.COLUMNS, loop_a), self.DIRECTIVE))
        verdict = gen.classify(self._table(self.COLUMNS, loop_a),
                               self._result(self.COLUMNS, loop_b, ["INTEGER"] + ["BIGINT"] * 5),
                               self.DIRECTIVE, None, self.SQL)
        self.assertEqual("flow_tie", verdict)


class TestEdgeDisjointPathsHaveNoRouteTie(unittest.TestCase):
    def test_edge_disjoint_paths_are_guarded_like_ksp(self):
        self.assertIn("pgr_edgedisjointpaths", gen.KSP_FUNCTIONS)


class TestContractionCompanion(unittest.TestCase):
    def _table(self, columns, rows):
        import pgparse

        return pgparse.AlignedTable(columns, rows, len(rows))

    FULL = ["type", "id", "contracted_vertices", "source", "target", "cost"]

    def test_which_calls_take_which_invariant(self):
        for name in ("pgr_contraction", "pgr_contractionDeadEnd", "pgr_contractionLinear"):
            with self.subTest(name=name):
                sql = "SELECT * FROM {}('SELECT 1')".format(name)
                self.assertTrue(gen.is_contraction_call(sql))
                self.assertFalse(gen.is_hierarchy_call(sql))
        sql = "SELECT * FROM pgr_contractionHierarchies('SELECT 1')"
        self.assertTrue(gen.is_hierarchy_call(sql))
        self.assertFalse(gen.is_contraction_call(sql))
        self.assertFalse(gen.is_contraction_call("SELECT * FROM pgr_dijkstra('SELECT 1', 1, 2)"))

    def test_companion_sql_drops_shortcut_ids_and_direction(self):
        sql, directive = gen.contraction_companion("SELECT * FROM pgr_contraction('x', false);\n", self.FULL)
        self.assertEqual(
            "SELECT type, CASE WHEN type = 'v' THEN id ELSE 0 END, "
            "CAST(list_sort(contracted_vertices) AS VARCHAR), least(source, target), "
            "greatest(source, target), cost\n"
            "FROM (SELECT * FROM pgr_contraction('x', false))\nORDER BY ALL;",
            sql)
        self.assertEqual("TITIIR", directive)

    def test_companion_sql_follows_a_narrower_select_list(self):
        sql, directive = gen.contraction_companion(
            "SELECT type, id, contracted_vertices FROM pgr_contraction('x')", ["type", "id", "contracted_vertices"])
        self.assertEqual(
            "SELECT type, CASE WHEN type = 'v' THEN id ELSE 0 END, "
            "CAST(list_sort(contracted_vertices) AS VARCHAR)\n"
            "FROM (SELECT type, id, contracted_vertices FROM pgr_contraction('x'))\nORDER BY ALL;",
            sql)
        self.assertEqual("TIT", directive)

    def test_companion_sql_rejects_part_of_an_edge(self):
        with self.assertRaises(ValueError):
            gen.contraction_companion("SELECT 1", ["type", "contracted_vertices", "source"])
        with self.assertRaises(ValueError):
            gen.contraction_companion("SELECT 1", ["id", "source", "target", "cost"])

    def test_companion_rows_sort_like_order_by_all(self):
        # Upstream's contraction.pg q1 after respell_list_cells: two shortcuts point the other way
        # in this build's order, which the companion no longer sees.
        table = self._table(self.FULL, [
            ["v", " 4", "[2]", "-1", "-1", "-1"],
            ["v", " 7", "[3, 1]", "-1", "-1", "-1"],
            ["e", "-1", "[5, 6]", "7", "10", "2"],
            ["e", "-2", "[17]", "16", "12", "2"],
        ])
        self.assertEqual(
            [["e", "0", "[17]", "12", "16", "2"],
             ["e", "0", "[5, 6]", "7", "10", "2"],
             ["v", "4", "[2]", "-1", "-1", "-1"],
             ["v", "7", "[1, 3]", "-1", "-1", "-1"]],
            gen.contraction_companion_rows(table, self.FULL))

    def test_actual_rows_render_like_the_expected_ones(self):
        columns = ["type", "id", "cv", "least", "greatest", "cost"]
        result = duckdbcli.QueryResult(columns, ["VARCHAR", "BIGINT", "VARCHAR", "BIGINT", "BIGINT", "DOUBLE"],
                                       [["e", 0, "[17]", 12, 16, 2.0], ["v", 4, "[2]", -1, -1, -1.0]])
        self.assertEqual([["e", "0", "[17]", "12", "16", "2"], ["v", "4", "[2]", "-1", "-1", "-1"]],
                         gen.render_actual_rows(result, "TITIIR"))


class TestHierarchyCompanion(unittest.TestCase):
    COLUMNS = ["type", "id", "contracted_vertices", "source", "target", "cost", "metric", "vertex_order"]

    def _table(self, rows):
        import pgparse

        return pgparse.AlignedTable(self.COLUMNS, rows, len(rows))

    def test_companion_sql_never_names_a_ranking_value(self):
        self.assertEqual(
            "SELECT CAST(list_sort(list(id) FILTER (WHERE type = 'v')) AS VARCHAR),\n"
            "       count(*) FILTER (WHERE type = 'v' AND len(contracted_vertices) > 0),\n"
            "       min(vertex_order) FILTER (WHERE type = 'v'), max(vertex_order) FILTER (WHERE type = 'v'),\n"
            "       count(DISTINCT vertex_order) FILTER (WHERE type = 'v'),\n"
            "       count(*) FILTER (WHERE type = 'e' AND (len(contracted_vertices) = 0 OR metric <> -1 "
            "OR vertex_order <> -1)),\n"
            "       count(*) FILTER (WHERE type = 'e') = -coalesce(min(id) FILTER (WHERE type = 'e'), 0)\n"
            "FROM (SELECT * FROM pgr_contractionHierarchies('x'));",
            gen.hierarchy_companion_sql("SELECT * FROM pgr_contractionHierarchies('x');\n"))

    def test_companion_rows_come_from_upstreams_table(self):
        table = self._table([
            ["v", " 2", "[]", "-1", "-1", "-1", "-1", " 3"],
            ["v", " 1", "[]", "-1", "-1", "-1", " 0", " 1"],
            ["v", " 3", "[]", "-1", "-1", "-1", "-2", " 2"],
            ["e", "-1", "[2]", "1", "3", "2", "-1", "-1"],
        ])
        self.assertEqual([["[1, 2, 3]", "0", "1", "3", "3", "0", "true"]], gen.hierarchy_companion_rows(table))

    def test_an_empty_answer(self):
        self.assertEqual([["NULL", "0", "NULL", "NULL", "0", "0", "true"]], gen.hierarchy_companion_rows(self._table([])))

    def test_a_narrower_select_list_is_rejected(self):
        import pgparse

        with self.assertRaises(ValueError):
            gen.hierarchy_companion_rows(pgparse.AlignedTable(["type", "id"], [], 0))

    def test_actual_rows_render_booleans_and_nulls(self):
        result = duckdbcli.QueryResult(["a", "b", "c", "d", "e", "f", "g"],
                                       ["VARCHAR", "BIGINT", "BIGINT", "BIGINT", "BIGINT", "BIGINT", "BOOLEAN"],
                                       [[None, 0, None, None, 0, 0, True]])
        self.assertEqual([["NULL", "0", "NULL", "NULL", "0", "0", "true"]],
                         gen.render_actual_rows(result, gen.HIERARCHY_DIRECTIVE))


if __name__ == "__main__":
    unittest.main()
