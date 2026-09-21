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


class CandidateKeysTest(unittest.TestCase):
    def test_named_form_appends_named_types_sorted_by_name(self):
        keys = cs.candidate_keys(["col0", "col1", "col2", "directed"],
                                 ["VARCHAR", "BIGINT", "BIGINT", "BOOLEAN"])
        self.assertIn(("VARCHAR", "BIGINT", "BIGINT", "BOOLEAN"), keys)
        self.assertIn(("VARCHAR", "BIGINT", "BIGINT"), keys)

    def test_positional_form_key_matches_the_upstream_argument_list(self):
        keys = cs.candidate_keys(["col0", "col1", "col2", "col3", "directed"],
                                 ["VARCHAR", "BIGINT", "BIGINT", "BOOLEAN", "BOOLEAN"])
        self.assertIn(("VARCHAR", "BIGINT", "BIGINT", "BOOLEAN"), keys)

    def test_a_variant_without_named_parameters_yields_one_key(self):
        self.assertEqual(cs.candidate_keys(["col0", "col1"], ["VARCHAR", "BIGINT"]),
                         {("VARCHAR", "BIGINT")})

    def test_list_types_survive_the_cli_quoting(self):
        keys = cs.candidate_keys(["col0", "col1", "col2", "directed"],
                                 ["VARCHAR", "'BIGINT[]'", "'BIGINT[]'", "BOOLEAN"])
        self.assertIn(("VARCHAR", "BIGINT[]", "BIGINT[]", "BOOLEAN"), keys)

    def test_rejects_mismatched_lengths(self):
        with self.assertRaises(ValueError):
            cs.candidate_keys(["col0"], ["VARCHAR", "BIGINT"])


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


if __name__ == "__main__":
    unittest.main()
