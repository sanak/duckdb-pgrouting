# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate sqllogictests from pgRouting's documentation queries.

For every ``/* -- qN */`` block of an upstream ``.pg`` file this tool rewrites the upstream
function names to this extension's public names, runs the rewritten query against the built
duckdb binary, and compares the answer with upstream's committed ``.result`` transcript. A block
that agrees is emitted with upstream's own expected rows; a block that disagrees stops the
generator. Task 7 adds the equal-cost tie classification on top of that.

The upstream-to-public name mapping is never written down here: it is read back from the
``pgrouting_name`` function tag in ``duckdb_functions()``.
"""

from __future__ import annotations

import argparse
import difflib
import json
import os
import pathlib
import re
import sys
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Sequence, Tuple, Union

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import duckdbcli  # noqa: E402
import export_sampledata  # noqa: E402
import pgparse  # noqa: E402

DOCQUERIES = "third_party/pgrouting/docqueries"
OUT_ROOT = "test/sql/pgrouting"
SKIP_FILE = "test/pgrouting_skip.json"

# A call is an upstream function only when pgr_ starts an identifier, so my_pgr_dijkstra_helper
# is left alone.
CALL_RE = re.compile(r"(?<![A-Za-z0-9_])(pgr_\w+)\s*\(", re.IGNORECASE)

_INTEGER_TYPES = {
    "TINYINT", "SMALLINT", "INTEGER", "BIGINT", "HUGEINT",
    "UTINYINT", "USMALLINT", "UINTEGER", "UBIGINT", "UHUGEINT",
}
_FLOAT_TYPES = {"FLOAT", "REAL", "DOUBLE"}


class Mismatch(RuntimeError):
    """This build's answer differs from upstream's committed result."""


@dataclass
class Emitted:
    name: str
    directive: str
    sql: str
    rows: List[List[str]]


@dataclass
class Skipped:
    name: str
    reason: str


Item = Union[Emitted, Skipped]


def implemented_names(db: duckdbcli.DuckDB) -> Dict[str, str]:
    """Lowercase upstream name to public name, straight from the catalog tag."""
    result = db.query(
        "SELECT DISTINCT lower(tags['pgrouting_name']) AS upstream, function_name AS public "
        "FROM duckdb_functions() "
        "WHERE tags['ext'] = 'routing' AND tags['pgrouting_name'] IS NOT NULL"
    )
    return {row[0]: row[1] for row in result.rows}


def translate(sql: str, implemented: Dict[str, str]) -> Tuple[str, List[str]]:
    """Rewrite upstream calls to public ones, keeping the documentation's casing."""
    missing: List[str] = []

    def replace(match: "re.Match[str]") -> str:
        upstream = match.group(1)
        if upstream.lower() in implemented:
            return upstream[len("pgr_"):] + "("
        missing.append(upstream)
        return match.group(0)

    return CALL_RE.sub(replace, sql), missing


def slt_types(duck_types: Sequence[str]) -> str:
    """The sqllogictest directive characters for a result's DuckDB types."""
    out = []
    for duck_type in duck_types:
        base = duck_type.split("(")[0].strip().upper()
        if base in _INTEGER_TYPES:
            out.append("I")
        elif base in _FLOAT_TYPES or base == "DECIMAL":
            out.append("R")
        else:
            out.append("T")
    return "".join(out)


def coerce(cell: Optional[str], slt_type: str) -> Any:
    """One upstream text cell as the value it denotes.

    psql renders a NULL and an empty string identically, so a blank is read as NULL and a caller
    that has a text column must refuse the block rather than guess.
    """
    if cell is None:
        return None
    text = cell.strip()
    if text == "":
        return None
    if slt_type == "I":
        return int(text)
    if slt_type == "R":
        return float(text)
    if text == "t":
        return True
    if text == "f":
        return False
    return text


def _actual(value: Any, slt_type: str) -> Any:
    """One value from DuckDB, brought onto the same footing as a coerced upstream cell."""
    if value is None:
        return None
    if slt_type == "I":
        return int(value)
    if slt_type == "R":
        return float(value)
    if isinstance(value, bool):
        return value
    return str(value)


def compare(table: pgparse.AlignedTable, result: duckdbcli.QueryResult, directive: str) -> None:
    """Raise Mismatch unless this build reproduces upstream's rows exactly."""
    expected = [[coerce(cell, t) for cell, t in zip(row, directive)] for row in table.rows]
    actual = [[_actual(v, t) for v, t in zip(row, directive)] for row in result.rows]
    if expected != actual:
        raise Mismatch(
            "\n".join(
                difflib.unified_diff(
                    [repr(r) for r in expected],
                    [repr(r) for r in actual],
                    fromfile="upstream",
                    tofile="this build",
                    lineterm="",
                )
            )
        )


HEADER = """# name: {out}
# description: Generated from pgRouting's {stem} documentation queries
# group: [pgrouting]

# SPDX-License-Identifier: GPL-2.0-or-later
#
# GENERATED FILE - do not edit by hand. Every edit is lost on the next regeneration.
# Regenerate with:  GEN=ninja make release && python3 scripts/gen_docqueries_tests.py
#
# Source: {pg} and the committed transcript beside it. The queries and their expected rows are
# upstream's; the only edits are the dropped pgr_ prefix and psql's aligned output rewritten as
# sqllogictest rows. A block this generator could not carry over is recorded below as a skipped
# line with its reason, so what this file does not cover is visible here rather than implied.

require routing

"""


def render(category: str, stem: str, items: Sequence[Item]) -> str:
    out = "{}/{}/{}.test".format(OUT_ROOT, category, stem)
    parts = [
        HEADER.format(
            out=out,
            stem=stem,
            pg="{}/{}/{}.pg".format(DOCQUERIES, category, stem),
        )
    ]
    # LOADER_SQL is already written as sqllogictest body text (each statement introduced by its
    # own "statement ok" line), so it goes into the generated file verbatim rather than being
    # rewrapped line by line.
    parts.append(export_sampledata.LOADER_SQL.strip() + "\n\n")
    for item in items:
        if isinstance(item, Skipped):
            parts.append("# {}: skipped - {}\n\n".format(item.name, item.reason))
            continue
        rows = "".join("\t".join(cells) + "\n" for cells in item.rows)
        parts.append(
            "# {}\nquery {}\n{}\n----\n{}\n".format(
                item.name, item.directive, item.sql.strip(), rows
            )
        )
    return "".join(parts)


def expected_cells(table: pgparse.AlignedTable, directive: str) -> List[List[str]]:
    """Upstream's own cells, normalised only where sqllogictest needs it."""
    out = []
    for row in table.rows:
        cells = []
        for cell, slt_type in zip(row, directive):
            text = cell.strip()
            cells.append("NULL" if text == "" else text)
        out.append(cells)
    return out


def process(category: str, stem: str, db: duckdbcli.DuckDB, implemented: Dict[str, str],
            skips: Dict[str, Dict[str, str]]) -> List[Item]:
    root = pathlib.Path(DOCQUERIES) / category
    pg_blocks = pgparse.nonempty(pgparse.split_blocks((root / (stem + ".pg")).read_text()))
    transcripts = {
        block.name: pgparse.parse_result_block(block.name, block.sql)
        for block in pgparse.split_blocks((root / (stem + ".result")).read_text())
    }
    file_skips = skips.get("{}/{}.pg".format(category, stem), {})

    items: List[Item] = []
    for block in pg_blocks:
        if block.name in file_skips:
            items.append(Skipped(block.name, file_skips[block.name]["reason"]))
            continue
        sql, missing = translate(block.sql, implemented)
        if missing:
            items.append(Skipped(block.name, "not implemented: " + ", ".join(sorted(set(missing)))))
            continue
        transcript = transcripts.get(block.name)
        if transcript is None or not transcript.tables:
            items.append(Skipped(block.name, "no result set"))
            continue
        if len(transcript.tables) > 1:
            items.append(Skipped(block.name, "more than one result set"))
            continue
        table = transcript.tables[0]
        result = db.query(sql)
        directive = slt_types(result.types)
        if any(t == "T" and cell.strip() == "" for row in table.rows for cell, t in zip(row, directive)):
            items.append(Skipped(block.name, "blank cell in a text column"))
            continue
        compare(table, result, directive)
        items.append(Emitted(block.name, directive, sql, expected_cells(table, directive)))
    return items


def generate(db: duckdbcli.DuckDB, only: Optional[str]) -> Dict[str, str]:
    """Render every docqueries file, returning {output path: file body}."""
    implemented = implemented_names(db)
    skips = json.loads(pathlib.Path(SKIP_FILE).read_text())
    rendered: Dict[str, str] = {}
    for pg in sorted(pathlib.Path(DOCQUERIES).glob("*/*.pg")):
        category, stem = pg.parent.name, pg.stem
        if only and category != only:
            continue
        if not (pg.parent / (stem + ".result")).exists():
            continue
        items = process(category, stem, db, implemented, skips)
        if not any(isinstance(item, Emitted) for item in items):
            continue
        rendered["{}/{}/{}.test".format(OUT_ROOT, category, stem)] = render(category, stem, items)
    return rendered


def _raw_preamble() -> str:
    """LOADER_SQL's statements as plain SQL, for driving the CLI directly.

    LOADER_SQL is written as sqllogictest body text -- each statement introduced by its own
    "statement ok" line -- because render() embeds it verbatim into the generated .test file.
    duckdbcli.DuckDB.preamble, by contrast, is plain SQL handed straight to the CLI (see
    test_duckdbcli.py's test_preamble_runs_before_every_query), so the directive lines that are
    not SQL are dropped here.
    """
    lines = [line for line in export_sampledata.LOADER_SQL.splitlines()
             if line.strip() != "statement ok"]
    return "\n".join(lines)


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--duckdb", default=duckdbcli.default_binary())
    parser.add_argument("--category", default=None)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)

    db = duckdbcli.DuckDB(args.duckdb, preamble=_raw_preamble())
    rendered = generate(db, args.category)

    failures = 0
    for path, body in sorted(rendered.items()):
        target = pathlib.Path(path)
        if args.check:
            current = target.read_text() if target.exists() else ""
            if current != body:
                failures += 1
                sys.stdout.writelines(
                    difflib.unified_diff(
                        current.splitlines(keepends=True),
                        body.splitlines(keepends=True),
                        fromfile=path + " (committed)",
                        tofile=path + " (regenerated)",
                    )
                )
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(body)
            print("wrote {}".format(path))
    if args.check and failures:
        print("\n{} generated file(s) are stale".format(failures), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
