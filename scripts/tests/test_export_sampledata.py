# SPDX-License-Identifier: GPL-2.0-or-later
"""Unit tests for the sample-graph fixture exporter."""

import contextlib
import io
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import export_sampledata  # noqa: E402

REPO = pathlib.Path(__file__).resolve().parents[2]


class ExportSampledataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tables = export_sampledata.build_tables(REPO)

    def test_list_literal_conversion(self):
        self.assertEqual("[4, 7]", export_sampledata.to_list_literal("{4,7}"))
        self.assertEqual("[6]", export_sampledata.to_list_literal("{6}"))
        # An absent array stays empty, which read_csv_auto reads back as NULL.
        self.assertEqual("", export_sampledata.to_list_literal(""))

    def test_edges_join_transcript_with_pg_literals(self):
        header, rows = self.tables["edges"]
        self.assertEqual(
            ["id", "source", "target", "cost", "reverse_cost", "capacity", "reverse_capacity"],
            header)
        self.assertEqual(18, len(rows))
        self.assertEqual(["1", "5", "6", "1.0", "1.0", "80", "130"], rows[0])
        self.assertEqual(["2", "6", "10", "-1.0", "1.0", "-1", "100"], rows[1])
        self.assertEqual([str(n) for n in range(1, 19)], [row[0] for row in rows])

    def test_vertices_arrays_and_nulls(self):
        header, rows = self.tables["vertices"]
        self.assertEqual(["id", "in_edges", "out_edges", "x", "y"], header)
        self.assertEqual(17, len(rows))
        self.assertEqual(["12", "[11, 12]", "[13]", "3", "3"], rows[11])
        # Vertex 1 is a source only: psql prints an empty in_edges cell.
        self.assertEqual(["1", "", "[6]", "0", "2"], rows[0])
        # The transcript's full precision survives verbatim.
        self.assertEqual("1.999999999999", rows[3][3])

    def test_pointsofinterest_renames_and_drops_geometry(self):
        header, rows = self.tables["pointsofinterest"]
        self.assertEqual(["pid", "edge_id", "side", "fraction", "distance"], header)
        self.assertEqual(6, len(rows))
        self.assertEqual(["3", "12", "l", "0.6", "0.2"], rows[2])

    def test_combinations_is_byte_identical_to_the_committed_file(self):
        committed = (REPO / "test/data/pgrouting_sample/combinations.csv").read_text(
            encoding="utf-8")
        self.assertEqual(committed, export_sampledata.render_csv(*self.tables["combinations"]))

    def test_restrictions_paths(self):
        header, rows = self.tables["restrictions"]
        self.assertEqual(["id", "path", "cost"], header)
        self.assertEqual(5, len(rows))
        self.assertEqual(["1", "[4, 7]", "100"], rows[0])
        self.assertEqual(["4", "[3, 5, 9]", "4"], rows[3])

    def test_render_csv_quotes_list_literals(self):
        rendered = export_sampledata.render_csv(*self.tables["restrictions"])
        self.assertTrue(rendered.startswith("id,path,cost\n"))
        self.assertIn('4,"[3, 5, 9]",4\n', rendered)
        self.assertNotIn("\r", rendered)

    def test_loader_sql_covers_every_fixture(self):
        for name in self.tables:
            self.assertIn(f"test/data/pgrouting_sample/{name}.csv", export_sampledata.LOADER_SQL)
        self.assertIn("CAST(in_edges AS BIGINT[])", export_sampledata.LOADER_SQL)
        self.assertIn("CAST(path AS BIGINT[])", export_sampledata.LOADER_SQL)

    def test_check_mode_is_quiet_and_clean_on_a_regenerated_tree(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            status = export_sampledata.main(["--check"])
        self.assertEqual("", out.getvalue())
        self.assertEqual(0, status)


if __name__ == "__main__":
    unittest.main()
