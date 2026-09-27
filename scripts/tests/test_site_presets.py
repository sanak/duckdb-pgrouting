# SPDX-License-Identifier: GPL-2.0-or-later
"""Every Playground preset runs natively: in file order, twice, inside its dataset's own catalog.

The Playground (site/) builds each dataset from site/datasets/<id>/dataset.json and offers the
queries in presets.json. A preset that stopped working would otherwise surface only in a reader's
browser. This test builds each dataset the way the page does -- an in-memory catalog attached
under the dataset's id and selected with USE, then the table SQL -- with the release binary, and
runs every preset in file order, each one twice, because a reader may run any preset again.
The table SQL names files "<id>/<file>", relative to test/data, which is where the data lives.
Skips when the binary is absent; spatial datasets and presets skip where spatial cannot be
installed (PGROUTING_NO_SPATIAL=1), and a dataset that reads from third-party servers runs only
with PGROUTING_NETWORK_TESTS=1.
"""

import json
import os
import pathlib
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import duckdbcli  # noqa: E402
import gen_docqueries_tests as gen  # noqa: E402

REPO = pathlib.Path(__file__).resolve().parents[2]
BINARY = REPO / "build/release/duckdb"
DATASETS = REPO / "site/datasets"
DATA = REPO / "test/data"

# The workshop chain builds 22,889 vertices, components and several cost matrices; seconds each.
TIMEOUT_SECONDS = 600

# A dataset with "network": true reads from third-party servers while its presets run (Overture's
# STAC catalogue and S3 bucket). Its chain runs only when this is 1: before a release, by hand.
NETWORK_ENV = "PGROUTING_NETWORK_TESTS"


def network_enabled():
    return os.environ.get(NETWORK_ENV, "") == "1"


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
    logic. Keep the two in step. A network dataset also gets httpfs and json: the browser reads
    https:// URLs and JSON without them, but the CLI needs httpfs, and the locally built one
    autoloads nothing.
    """
    lines = []
    if uses_spatial(dataset, presets):
        lines.append("INSTALL spatial;")
    if dataset.get("network", False):
        lines += ["INSTALL httpfs;", "LOAD httpfs;", "INSTALL json;", "LOAD json;"]
    lines.append("ATTACH IF NOT EXISTS ':memory:' AS {};".format(quote_ident(dataset_id)))
    lines.append("USE {};".format(quote_ident(dataset_id)))
    if dataset.get("spatial", False):
        lines.append("LOAD spatial;")
    for table in dataset["tables"]:
        lines.append("CREATE OR REPLACE TABLE {} AS {};".format(quote_ident(table["name"]), table["sql"]))
    return "\n".join(lines) + "\n"


def map_queries(dataset):
    """The dataset's map queries, each with the key it is registered under.

    Which keys a `map` object has depends on its `mode` (see site/src/datasets.ts parseMap):
    abstract has vertices/edges/points, geographic has edges/nodes. Read back every string value
    except `mode` and `attribution` instead of hard-coding either list, so the two stay in step
    (`view`, `dependsOn` and `inputs` are not strings).
    """
    return [(key, value) for key, value in dataset["map"].items()
            if key not in ("mode", "attribution") and isinstance(value, str)]


def builds_its_network(dataset):
    """The map draws tables the presets create (dataset.json map.dependsOn)."""
    return bool(dataset["map"].get("dependsOn"))


def chain_sql(dataset_id, dataset, presets):
    maps = [".print map {}\nSELECT count(*) FROM ({});\n".format(key, query.strip().rstrip(";"))
            for key, query in map_queries(dataset)]
    # A network the presets build exists only once they have run.
    before, after = ([], maps) if builds_its_network(dataset) else (maps, [])
    parts = [setup_sql(dataset_id, dataset, presets), *before]
    for preset in presets:
        body = preset_sql(preset).rstrip().rstrip(";") + ";\n"
        parts.append(".print preset {}\n{}".format(preset["id"], body))
        parts.append(".print again {}\n{}".format(preset["id"], body))
    return "".join(parts + after)


def skip_reason(dataset):
    """Why the native chain skips this dataset here, or None."""
    if dataset.get("network", False) and not network_enabled():
        return "network dataset skipped: set {}=1 to run it".format(NETWORK_ENV)
    if dataset.get("spatial", False) and gen.spatial_disabled():
        return "spatial dataset skipped: " + gen.NO_SPATIAL_ENV + "=1"
    return None


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
        dataset = {
            "tables": [{"name": "t", "sql": "SELECT 1"}],
            "map": {"mode": "abstract", "vertices": "SELECT 1", "edges": "SELECT 1", "points": "SELECT 1"},
        }
        chain = chain_sql("a", dataset, [{"id": "p", "sql": "SELECT 2;"}])
        self.assertTrue(chain.endswith(".print preset p\nSELECT 2;\n.print again p\nSELECT 2;\n"))

    def test_map_queries_run_once_after_setup_and_before_presets(self):
        dataset = {
            "tables": [{"name": "t", "sql": "SELECT 1"}],
            "map": {"mode": "abstract", "vertices": "SELECT 1", "edges": "SELECT 2", "points": "SELECT 3"},
        }
        chain = chain_sql("a", dataset, [{"id": "p", "sql": "SELECT 4;"}])
        self.assertIn(".print map vertices\nSELECT count(*) FROM (SELECT 1);\n", chain)
        self.assertIn(".print map edges\nSELECT count(*) FROM (SELECT 2);\n", chain)
        self.assertIn(".print map points\nSELECT count(*) FROM (SELECT 3);\n", chain)
        self.assertLess(chain.index(".print map points"), chain.index(".print preset p"))

    def test_geographic_map_skips_attribution(self):
        dataset = {
            "tables": [{"name": "t", "sql": "SELECT 1"}],
            "map": {"mode": "geographic", "edges": "SELECT 1", "nodes": "SELECT 2", "attribution": "© someone"},
        }
        self.assertEqual([("edges", "SELECT 1"), ("nodes", "SELECT 2")], map_queries(dataset))

    def test_setup_without_tables_attaches_and_uses(self):
        self.assertEqual('ATTACH IF NOT EXISTS \':memory:\' AS "o";\nUSE "o";\n',
                         setup_sql("o", {"spatial": False, "tables": []}, []))

    def test_a_network_dataset_installs_and_loads_httpfs_and_json_first(self):
        setup = setup_sql("o", {"network": True, "tables": []}, [])
        self.assertTrue(setup.startswith("INSTALL httpfs;\nLOAD httpfs;\nINSTALL json;\nLOAD json;\nATTACH "))

    def test_map_queries_of_a_network_the_presets_build_run_after_them(self):
        dataset = {
            "tables": [],
            "map": {"mode": "geographic", "edges": "SELECT 1", "nodes": "SELECT 2", "attribution": "©",
                    "view": [0, 0, 1, 1], "dependsOn": ["t"], "inputs": []},
        }
        chain = chain_sql("a", dataset, [{"id": "p", "sql": "SELECT 4;"}])
        self.assertEqual([("edges", "SELECT 1"), ("nodes", "SELECT 2")], map_queries(dataset))
        self.assertLess(chain.index(".print again p"), chain.index(".print map edges"))

    def test_network_datasets_run_only_when_opted_in(self):
        with mock.patch.dict(os.environ, {NETWORK_ENV: "", gen.NO_SPATIAL_ENV: ""}):
            self.assertIn(NETWORK_ENV, skip_reason({"network": True}))
            self.assertIsNone(skip_reason({"network": False, "spatial": True}))
        with mock.patch.dict(os.environ, {NETWORK_ENV: "1", gen.NO_SPATIAL_ENV: ""}):
            self.assertIsNone(skip_reason({"network": True, "spatial": True}))
        with mock.patch.dict(os.environ, {NETWORK_ENV: "1", gen.NO_SPATIAL_ENV: "1"}):
            self.assertIn(gen.NO_SPATIAL_ENV, skip_reason({"network": True, "spatial": True}))


@unittest.skipUnless(BINARY.exists(), "build/release/duckdb not built")
class TestSitePresets(unittest.TestCase):
    def setUp(self):
        self.addCleanup(os.chdir, os.getcwd())
        os.chdir(DATA)

    def test_both_datasets_are_committed(self):
        self.assertEqual(["sampledata", "workshop-hiroshima"], committed_datasets())

    def test_every_preset_runs_twice_in_order(self):
        db = duckdbcli.DuckDB(str(BINARY), flags=["-bail"])
        for dataset_id in committed_datasets():
            with self.subTest(dataset=dataset_id):
                dataset = read_json(dataset_id, "dataset.json")
                presets = read_json(dataset_id, "presets.json")["presets"]
                reason = skip_reason(dataset)
                if reason:
                    self.skipTest(reason)
                if gen.spatial_disabled():
                    presets = [p for p in presets if "LOAD spatial" not in preset_sql(p)]
                try:
                    db.script(chain_sql(dataset_id, dataset, presets), timeout=TIMEOUT_SECONDS)
                except duckdbcli.DuckDBError as error:
                    self.fail("{}: the last 'preset'/'again' line names the failing preset\n{}".format(
                        dataset_id, error))


if __name__ == "__main__":
    unittest.main()
