# SPDX-License-Identifier: GPL-2.0-or-later
"""Unit tests for the pure helpers of scripts/check_signatures.py."""

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import check_signatures as cs
import duckdbcli


class ParseSigFileTest(unittest.TestCase):
    def test_drops_internal_signatures(self):
        text = "pgr_dijkstra(text,bigint,bigint,boolean)\n_pgr_dijkstra(text,text,boolean,boolean)\n"
        self.assertEqual(cs.parse_sig_file(text),
                         {"pgr_dijkstra": [("text", "bigint", "bigint", "boolean")]})

    def test_keeps_multi_word_types_whole(self):
        text = "pgr_astarcost(text,anyarray,bigint,boolean,integer,double precision,double precision)\n"
        self.assertEqual(cs.parse_sig_file(text)["pgr_astarcost"],
                         [("text", "anyarray", "bigint", "boolean", "integer",
                           "double precision", "double precision")])

    def test_zero_argument_signature_is_an_empty_tuple(self):
        self.assertEqual(cs.parse_sig_file("pgr_version()\n"), {"pgr_version": [()]})

    def test_groups_overloads_and_ignores_blank_lines(self):
        text = "pgr_dijkstra(text,text,boolean)\n\npgr_dijkstra(text,bigint,bigint,boolean)\n"
        self.assertEqual(len(cs.parse_sig_file(text)["pgr_dijkstra"]), 2)

    def test_rejects_an_unparsable_line(self):
        with self.assertRaises(ValueError):
            cs.parse_sig_file("pgr_dijkstra text,bigint\n")


class MapUpstreamTypesTest(unittest.TestCase):
    def test_maps_every_type_the_mvp_uses(self):
        self.assertEqual(
            cs.map_upstream_types(("text", "anyarray", "bigint", "boolean", "integer",
                                   "double precision")),
            ("VARCHAR", "BIGINT[]", "BIGINT", "BOOLEAN", "INTEGER", "DOUBLE"))

    def test_raises_on_an_unmapped_type(self):
        with self.assertRaises(ValueError):
            cs.map_upstream_types(("geometry",))


class CoversTest(unittest.TestCase):
    DIJKSTRA = ("VARCHAR", "BIGINT", "BIGINT", "BOOLEAN")
    NEAR_AA = ("VARCHAR", "BIGINT[]", "BIGINT[]", "BOOLEAN", "BIGINT", "BOOLEAN")

    def test_named_directed_variant_covers_the_signature(self):
        self.assertTrue(cs.covers(["col0", "col1", "col2", "directed"],
                                  ["VARCHAR", "BIGINT", "BIGINT", "BOOLEAN"], self.DIJKSTRA))

    def test_positional_directed_variant_covers_the_signature(self):
        self.assertTrue(cs.covers(["col0", "col1", "col2", "col3", "directed"],
                                  ["VARCHAR", "BIGINT", "BIGINT", "BOOLEAN", "BOOLEAN"], self.DIJKSTRA))

    def test_named_parameters_reported_in_any_order_still_cover(self):
        # duckdb_functions() does not keep declaration order; cap sorts before directed.
        for p in (0, 1, 2, 3):
            positional = ["col%d" % i for i in range(3 + p)]
            positional_types = list(self.NEAR_AA[:3 + p])
            named = ["cap", "directed", "global"]
            named_types = ["BIGINT", "BOOLEAN", "BOOLEAN"]
            with self.subTest(p=p):
                self.assertTrue(cs.covers(positional + named, positional_types + named_types,
                                          self.NEAR_AA))

    def test_a_variant_with_a_missing_optional_does_not_cover(self):
        self.assertFalse(cs.covers(["col0", "col1", "col2", "directed"],
                                   ["VARCHAR", "BIGINT[]", "BIGINT[]", "BOOLEAN"], self.NEAR_AA))

    def test_a_different_required_type_does_not_cover(self):
        self.assertFalse(cs.covers(["col0", "col1", "col2", "directed"],
                                   ["VARCHAR", "BIGINT[]", "BIGINT", "BOOLEAN"], self.DIJKSTRA))

    def test_a_variant_without_named_parameters_covers_only_its_exact_list(self):
        self.assertTrue(cs.covers(["col0", "col1"], ["VARCHAR", "BIGINT"], ("VARCHAR", "BIGINT")))
        self.assertFalse(cs.covers(["col0", "col1"], ["VARCHAR", "BIGINT"],
                                   ("VARCHAR", "BIGINT", "BOOLEAN")))

    def test_list_types_survive_the_cli_quoting(self):
        self.assertTrue(cs.covers(["col0", "col1", "col2", "directed"],
                                  ["VARCHAR", "'BIGINT[]'", "'BIGINT[]'", "BOOLEAN"],
                                  ("VARCHAR", "BIGINT[]", "BIGINT[]", "BOOLEAN")))

    def test_rejects_mismatched_lengths(self):
        with self.assertRaises(ValueError):
            cs.split_variant(["col0"], ["VARCHAR", "BIGINT"])


class SigFilePathTest(unittest.TestCase):
    def test_uses_the_first_two_version_components(self):
        self.assertTrue(cs.sig_file_path("4.0.2").endswith(
            os.path.join("sigs", "pgrouting--4.0.sig")))

    def test_rejects_a_version_without_a_minor(self):
        with self.assertRaises(ValueError):
            cs.sig_file_path("4")


class LoadNotPortedTest(unittest.TestCase):
    def _write(self, text):
        handle = tempfile.NamedTemporaryFile("w", suffix=".json", delete=False)
        handle.write(text)
        handle.close()
        self.addCleanup(os.unlink, handle.name)
        return handle.name

    def test_missing_file_is_an_empty_mapping(self):
        self.assertEqual(cs.load_not_ported(os.path.join("no", "such", "file.json")), {})

    def test_reads_entries_with_a_reason(self):
        path = self._write('{"pgr_dijkstravia": {"reason": "out of the MVP function set"}}')
        self.assertEqual(cs.load_not_ported(path)["pgr_dijkstravia"]["reason"],
                         "out of the MVP function set")

    def test_rejects_an_entry_without_a_reason(self):
        path = self._write('{"pgr_dijkstravia": {}}')
        with self.assertRaises(ValueError):
            cs.load_not_ported(path)

    def test_rejects_a_top_level_list(self):
        path = self._write('["pgr_dijkstravia"]')
        with self.assertRaises(ValueError):
            cs.load_not_ported(path)


class CollectVariantsTest(unittest.TestCase):
    """collect_variants splits array_to_string(..., chr(31)) back into tuples.

    This split is new logic that exists only because DuckDB's `.mode json` cannot render a LIST
    column as valid JSON (it uses list-literal syntax such as `[col0, col1]`), so
    duckdbcli.DuckDB.query() fails on the brief's original query that selects `parameters` and
    `parameter_types` directly. collect_variants works around that by joining each list into one
    VARCHAR with chr(31) and splitting it back apart here -- these tests pin down that round trip
    with a stub `db`, not a live DuckDB process.
    """

    class _FakeDB:
        def __init__(self, rows):
            self._rows = rows

        def query(self, sql):
            return duckdbcli.QueryResult(
                columns=["pgrouting_name", "parameters", "parameter_types"],
                types=["VARCHAR", "VARCHAR", "VARCHAR"],
                rows=self._rows,
            )

    def test_splits_a_multi_element_list(self):
        db = self._FakeDB([["pgr_dijkstra", "col0\x1fcol1\x1fdirected",
                            "VARCHAR\x1fBIGINT\x1fBOOLEAN"]])
        self.assertEqual(cs.collect_variants(db),
                         {"pgr_dijkstra": [(("col0", "col1", "directed"),
                                            ("VARCHAR", "BIGINT", "BOOLEAN"))]})

    def test_splits_a_single_element_list(self):
        db = self._FakeDB([["pgr_foo", "col0", "VARCHAR"]])
        self.assertEqual(cs.collect_variants(db),
                         {"pgr_foo": [(("col0",), ("VARCHAR",))]})

    def test_empty_parameter_list_is_an_empty_tuple_not_a_tuple_of_one_empty_string(self):
        db = self._FakeDB([["pgr_version", "", ""]])
        self.assertEqual(cs.collect_variants(db), {"pgr_version": [((), ())]})

    def test_groups_multiple_rows_under_the_same_function_name(self):
        db = self._FakeDB([
            ["pgr_dijkstra", "col0\x1fcol1\x1fdirected", "VARCHAR\x1fBIGINT\x1fBOOLEAN"],
            ["pgr_dijkstra", "col0\x1fcol1", "VARCHAR\x1fBIGINT"],
        ])
        self.assertEqual(len(cs.collect_variants(db)["pgr_dijkstra"]), 2)

    def test_folds_a_camelcase_tag_to_lowercase(self):
        # PostgreSQL case-folds unquoted identifiers, so upstream's own
        # `CREATE FUNCTION pgr_dijkstraCost(...)` is named `pgr_dijkstracost` there, and that is
        # the spelling the .sig file records; the tag this extension carries keeps upstream's
        # CREATE FUNCTION spelling verbatim, so collect_variants must fold it to match.
        db = self._FakeDB([["pgr_dijkstraCost", "col0", "VARCHAR"]])
        variants = cs.collect_variants(db)
        self.assertIn("pgr_dijkstracost", variants)
        self.assertNotIn("pgr_dijkstraCost", variants)


if __name__ == "__main__":
    unittest.main()
