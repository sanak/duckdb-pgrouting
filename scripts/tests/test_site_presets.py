# SPDX-License-Identifier: GPL-2.0-or-later
"""Every Playground preset runs natively: in file order, twice, inside its dataset's own catalog.

The Playground (site/) builds each dataset from site/datasets/<id>/dataset.json and offers the
queries in presets.json. A preset that stopped working would otherwise surface only in a reader's
browser. This test builds each dataset the way the page does -- an in-memory catalog attached
under the dataset's id and selected with USE, then the table SQL -- with the release binary, and
runs every preset in file order, each one twice, because a reader may run any preset again.
The table SQL names files "<id>/<file>", relative to test/data, which is where the data lives.
Skips when the binary is absent; spatial datasets and presets skip where spatial cannot be
installed (PGROUTING_NO_SPATIAL=1).
"""

import json
import os
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import duckdbcli  # noqa: E402
import gen_docqueries_tests as gen  # noqa: E402

REPO = pathlib.Path(__file__).resolve().parents[2]
BINARY = REPO / "build/release/duckdb"
DATASETS = REPO / "site/datasets"
DATA = REPO / "test/data"

# The workshop chain builds 22,889 vertices, components and several cost matrices; seconds each.
TIMEOUT_SECONDS = 600


def quote_ident(name):
    """Mirrors site/src/datasets.ts quoteIdent(); stdlib-only Python cannot import TypeScript.

    Keep the two in step.
    """
    return '"' + name.replace('"', '""') + '"'


def preset_sql(preset):
    sql = preset["sql"]
    return "\n".join(sql) if isinstance(sql, list) else sql


def uses_spatial(dataset, presets):
    return dataset.get("spatial", False) or any("LOAD spatial" in preset_sql(p) for p in presets)


def setup_sql(dataset_id, dataset, presets):
    """Mirrors site/src/datasets.ts setupStatements(), plus INSTALL spatial when anything loads it.

    stdlib-only Python cannot import TypeScript, so this deliberately duplicates that function's
    logic. Keep the two in step.
    """
    lines = []
    if uses_spatial(dataset, presets):
        lines.append("INSTALL spatial;")
    lines.append("ATTACH IF NOT EXISTS ':memory:' AS {};".format(quote_ident(dataset_id)))
    lines.append("USE {};".format(quote_ident(dataset_id)))
    if dataset.get("spatial", False):
        lines.append("LOAD spatial;")
    for table in dataset["tables"]:
        lines.append("CREATE OR REPLACE TABLE {} AS {};".format(quote_ident(table["name"]), table["sql"]))
    return "\n".join(lines) + "\n"


def chain_sql(dataset_id, dataset, presets):
    parts = [setup_sql(dataset_id, dataset, presets)]
    for preset in presets:
        body = preset_sql(preset).rstrip().rstrip(";") + ";\n"
        parts.append(".print preset {}\n{}".format(preset["id"], body))
        parts.append(".print again {}\n{}".format(preset["id"], body))
    return "".join(parts)


def committed_datasets():
    return sorted(p.name for p in DATASETS.iterdir() if (p / "dataset.json").exists())


def read_json(dataset_id, name):
    return json.loads((DATASETS / dataset_id / name).read_text())


class TestChainSql(unittest.TestCase):
    """The script shape; needs no binary."""

    def test_setup_attaches_uses_and_creates(self):
        dataset = {"spatial": False, "tables": [{"name": "t", "sql": "SELECT 1"}]}
        self.assertEqual(
            'ATTACH IF NOT EXISTS \':memory:\' AS "a-b";\nUSE "a-b";\n'
            'CREATE OR REPLACE TABLE "t" AS SELECT 1;\n',
            setup_sql("a-b", dataset, []),
        )

    def test_spatial_is_installed_when_a_preset_loads_it(self):
        dataset = {"spatial": False, "tables": [{"name": "t", "sql": "SELECT 1"}]}
        presets = [{"id": "p", "sql": ["LOAD spatial;", "SELECT 1;"]}]
        self.assertTrue(setup_sql("a", dataset, presets).startswith("INSTALL spatial;\n"))
        self.assertNotIn("\nLOAD spatial;", setup_sql("a", dataset, presets))

    def test_every_preset_runs_twice_after_a_marker(self):
        dataset = {"tables": [{"name": "t", "sql": "SELECT 1"}]}
        chain = chain_sql("a", dataset, [{"id": "p", "sql": "SELECT 2;"}])
        self.assertTrue(chain.endswith(".print preset p\nSELECT 2;\n.print again p\nSELECT 2;\n"))


@unittest.skipUnless(BINARY.exists(), "build/release/duckdb not built")
class TestSitePresets(unittest.TestCase):
    def setUp(self):
        self.addCleanup(os.chdir, os.getcwd())
        os.chdir(DATA)

    def test_both_datasets_are_committed(self):
        self.assertIn("sampledata", committed_datasets())

    def test_every_preset_runs_twice_in_order(self):
        db = duckdbcli.DuckDB(str(BINARY), flags=["-bail"])
        for dataset_id in committed_datasets():
            with self.subTest(dataset=dataset_id):
                dataset = read_json(dataset_id, "dataset.json")
                presets = read_json(dataset_id, "presets.json")["presets"]
                if gen.spatial_disabled():
                    if dataset.get("spatial", False):
                        self.skipTest("spatial dataset skipped: " + gen.NO_SPATIAL_ENV + "=1")
                    presets = [p for p in presets if "LOAD spatial" not in preset_sql(p)]
                try:
                    db.script(chain_sql(dataset_id, dataset, presets), timeout=TIMEOUT_SECONDS)
                except duckdbcli.DuckDBError as error:
                    self.fail("{}: the last 'preset'/'again' line names the failing preset\n{}".format(
                        dataset_id, error))


if __name__ == "__main__":
    unittest.main()
