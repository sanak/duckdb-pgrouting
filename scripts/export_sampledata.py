# SPDX-License-Identifier: GPL-2.0-or-later
"""Rebuild test/data/sampledata/*.csv from pgRouting's committed sample data.

Upstream builds its sample graph by running ``tools/testers/sampledata.pg`` against a
PostgreSQL+PostGIS database: ``pgr_extractVertices`` derives the vertices, and ``ST_StartPoint``
/ ``ST_EndPoint`` fill in each edge's ``source`` and ``target``. This extension depends on no
spatial type and on no database, so none of that can run here -- but it does not have to, because
upstream also commits ``docqueries/src/sampledata.result``, the psql transcript of exactly that
run. Every derived value is printed there in psql's aligned output, so the fixtures are read out
of the transcript instead of recomputed.

The one thing the transcript does not print is the four literal columns of the edges INSERT
(``cost``, ``reverse_cost``, ``capacity``, ``reverse_capacity``); those come from the ``.pg``
file, joined to the transcript on ``id``. ``edges.id`` is a BIGSERIAL filled by a single
multi-row INSERT, so id = row index + 1.

Geometry is dropped on purpose: no query in this repository can consume it. The ``x``/``y``
columns of ``vertices`` survive, which is everything the sample graph's coordinates are used for.
"""

from __future__ import annotations

import argparse
import csv
import difflib
import io
import pathlib
import re
import sys
from typing import Dict, List, Tuple

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import pgparse  # noqa: E402

PG_FILE = pathlib.Path("third_party/pgrouting/tools/testers/sampledata.pg")
RESULT_FILE = pathlib.Path("third_party/pgrouting/docqueries/src/sampledata.result")
OUT_DIR = pathlib.Path("test/data/sampledata")

EDGES_START = "/* --EDGE TABLE ADD DATA start */"
EDGES_END = "/* --EDGE TABLE ADD DATA end */"

# "( 1,  1,  80, 130,   ST_MakeLine(...))," -- cost, reverse_cost, capacity, reverse_capacity.
_EDGE_ROW_RE = re.compile(r"^\(\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),")

# Emitted verbatim into every generated sqllogictest. The two array columns ship as text because
# read_csv_auto has no list inference; CAST('[4, 7]' AS BIGINT[]) reads them back, and an empty
# field stays NULL. restrictions.cost is cast because every value is whole and would otherwise be
# inferred BIGINT, where upstream's column is FLOAT.
LOADER_SQL = """\
statement ok
CREATE TABLE edges AS
  SELECT * FROM read_csv_auto('test/data/sampledata/edges.csv');

statement ok
CREATE TABLE vertices AS
  SELECT id, CAST(in_edges AS BIGINT[]) AS in_edges,
         CAST(out_edges AS BIGINT[]) AS out_edges, x, y
  FROM read_csv_auto('test/data/sampledata/vertices.csv');

statement ok
CREATE TABLE pointsofinterest AS
  SELECT * FROM read_csv_auto('test/data/sampledata/pointsofinterest.csv');

statement ok
CREATE TABLE combinations AS
  SELECT * FROM read_csv_auto('test/data/sampledata/combinations.csv');

statement ok
CREATE TABLE restrictions AS
  SELECT id, CAST(path AS BIGINT[]) AS path, CAST(cost AS DOUBLE) AS cost
  FROM read_csv_auto('test/data/sampledata/restrictions.csv');
"""

Rows = List[List[str]]
Tables = Dict[str, Tuple[List[str], Rows]]


def to_list_literal(cell: str) -> str:
    """Convert a PostgreSQL array cell to a DuckDB list literal: ``{4,7}`` -> ``[4, 7]``."""
    if not cell:
        return ""
    return "[" + ", ".join(part for part in cell.strip("{}").split(",") if part) + "]"


def _result_tables(repo: pathlib.Path) -> Dict[str, List[pgparse.AlignedTable]]:
    """Every aligned table in sampledata.result, grouped by the marker that introduced it."""
    text = (repo / RESULT_FILE).read_text(encoding="utf-8")
    found: Dict[str, List[pgparse.AlignedTable]] = {}
    for block in pgparse.split_blocks(text):
        tables = pgparse.parse_result_block(block.name, block.sql).tables
        found.setdefault(block.name, []).extend(tables)
    return found


def _one_table(found: Dict[str, List[pgparse.AlignedTable]], marker: str) -> pgparse.AlignedTable:
    """The single table under ``marker``, or a hard failure.

    Marker q1-1 carries psql's "Table public.vertices" description, which looks like a table but
    ends without a "(N rows)" line; parse_aligned returns (None, i) for it, so q1-1 yields one
    table, not two. Demanding exactly one here makes any upstream reshuffle fail loudly.
    """
    tables = found.get(marker, [])
    if len(tables) != 1:
        raise SystemExit(
            f"{RESULT_FILE}: marker {marker} carries {len(tables)} aligned tables, expected 1")
    return tables[0]


def _project(table: pgparse.AlignedTable, columns: List[str]) -> Rows:
    missing = [name for name in columns if name not in table.columns]
    if missing:
        raise SystemExit(f"{RESULT_FILE}: columns {missing} absent from {table.columns}")
    picks = [table.columns.index(name) for name in columns]
    # pgparse.parse_aligned only drops psql's own one-space margin, deliberately leaving a
    # right-aligned number's (or array literal's) extra left padding for the consumer to strip:
    # this fixture is plain ids, numbers and array literals with no genuine leading or trailing
    # whitespace to preserve, unlike a documentation query's text sentences, so a full strip here
    # is exactly right.
    return [[row[pick].strip() for pick in picks] for row in table.rows]


def _edge_literals(repo: pathlib.Path) -> Rows:
    """cost, reverse_cost, capacity and reverse_capacity, in id order, from the .pg INSERT."""
    lines = (repo / PG_FILE).read_text(encoding="utf-8").splitlines()
    body = lines[lines.index(EDGES_START) : lines.index(EDGES_END)]
    rows: Rows = []
    for line in body:
        match = _EDGE_ROW_RE.match(line.strip())
        if match:
            cost, reverse_cost, capacity, reverse_capacity = match.groups()
            # The two cost columns are FLOAT upstream; the two capacity columns are BIGINT.
            rows.append([str(float(cost)), str(float(reverse_cost)), capacity, reverse_capacity])
    return rows


def build_tables(repo: pathlib.Path) -> Tables:
    """Every fixture as (header, rows), keyed by its CSV basename."""
    found = _result_tables(repo)
    tables: Tables = {}

    ids = _project(_one_table(found, "q4"), ["id", "source", "target"])
    literals = _edge_literals(repo)
    if len(ids) != len(literals):
        raise SystemExit(f"edges: {len(ids)} rows in marker q4, {len(literals)} in {PG_FILE}")
    tables["edges"] = (
        ["id", "source", "target", "cost", "reverse_cost", "capacity", "reverse_capacity"],
        [left + right for left, right in zip(ids, literals)],
    )

    vertices = _project(_one_table(found, "q2"), ["id", "in_edges", "out_edges", "x", "y"])
    tables["vertices"] = (
        ["id", "in_edges", "out_edges", "x", "y"],
        [[row[0], to_list_literal(row[1]), to_list_literal(row[2]), row[3], row[4]]
         for row in vertices],
    )

    tables["pointsofinterest"] = (
        ["pid", "edge_id", "side", "fraction", "distance"],
        _project(_one_table(found, "p6"), ["pid", "edge_id", "side", "frac", "dist"]),
    )

    tables["combinations"] = (
        ["source", "target"],
        _project(_one_table(found, "c3"), ["source", "target"]),
    )

    restrictions = _project(_one_table(found, "r3"), ["id", "path", "cost"])
    tables["restrictions"] = (
        ["id", "path", "cost"],
        [[row[0], to_list_literal(row[1]), row[2]] for row in restrictions],
    )
    return tables


def render_csv(header: List[str], rows: Rows) -> str:
    """One CSV document with a header row and LF line endings, whatever the platform."""
    out = io.StringIO()
    writer = csv.writer(out, lineterminator="\n")
    writer.writerow(header)
    writer.writerows(rows)
    return out.getvalue()


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true",
                        help="diff against the committed files instead of writing them")
    args = parser.parse_args(argv)

    repo = pathlib.Path(__file__).resolve().parents[1]
    tables = build_tables(repo)
    stale = 0
    for name in sorted(tables):
        header, rows = tables[name]
        path = repo / OUT_DIR / f"{name}.csv"
        fresh = render_csv(header, rows)
        if args.check:
            committed = path.read_text(encoding="utf-8") if path.exists() else ""
            diff = list(difflib.unified_diff(
                committed.splitlines(keepends=True), fresh.splitlines(keepends=True),
                fromfile=f"a/{OUT_DIR}/{name}.csv", tofile=f"b/{OUT_DIR}/{name}.csv"))
            if diff:
                stale += 1
                sys.stdout.writelines(diff)
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(fresh, encoding="utf-8")
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
