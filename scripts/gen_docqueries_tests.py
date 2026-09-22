# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate sqllogictests from pgRouting's documentation queries.

For every ``/* -- qN */`` block of an upstream ``.pg`` file this tool rewrites the upstream
function names to this extension's public names, runs the rewritten query against the built
duckdb binary, and compares the answer with upstream's committed ``.result`` transcript. A block
that agrees is emitted with upstream's own expected rows; a block that disagrees stops the
generator. A block that differs only by an equal-cost route is downgraded to a tie-insensitive
assertion and recorded in test/pgrouting_ties.json.

Only each implemented function's own documentation page is processed (see select_stems);
--category narrows that further for debugging.

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
import textwrap
from dataclasses import dataclass
from itertools import zip_longest
from typing import Any, Dict, List, Optional, Sequence, Set, Tuple, Union

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import duckdbcli  # noqa: E402
import export_sampledata  # noqa: E402
import pgparse  # noqa: E402

DOCQUERIES = "third_party/pgrouting/docqueries"
OUT_ROOT = "test/sql/pgrouting"
SKIP_FILE = "test/pgrouting_skip.json"
TIES_FILE = "test/pgrouting_ties.json"
TIE_COLUMNS = ("start_vid", "end_vid", "agg_cost")
ROUTE_COLUMNS = ("node", "edge")

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
    note: str = ""


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
    that has a text column must refuse the block rather than guess. pgparse.parse_aligned only
    drops psql's own one-space margin and trailing padding, deliberately keeping a right-aligned
    number's extra left padding and a text value's own genuine leading space (see its docstring):
    an "I"/"R" cell, which can never have a genuine leading space, is fully stripped here because
    this layer is the one that knows the column's type; a "T" cell is returned exactly as
    parse_aligned produced it, so a value like ' visits' round-trips.
    """
    if cell is None:
        return None
    if cell.strip() == "":
        return None
    if slt_type == "I":
        return int(cell.strip())
    if slt_type == "R":
        return float(cell.strip())
    if cell.strip() == "t":
        return True
    if cell.strip() == "f":
        return False
    return cell


def _actual(value: Any, slt_type: str, float_digits: Optional[int] = None) -> Any:
    """One value from DuckDB, brought onto the same footing as a coerced upstream cell.

    ``float_digits`` is the page's ``SET extra_float_digits`` value (``pgparse.extra_float_digits``),
    or None when the page never set it. PostgreSQL's ``float8out`` prints ``DBL_DIG`` (15)
    ``+ extra_float_digits`` significant digits when that setting is not positive, which is why a
    handful of pages' committed transcripts show a clean ``0.3`` for a value this build's IEEE
    double computes as ``0.30000000000000004`` (the classic ``1.0 - 0.7`` artifact): upstream's
    psql rounded its display before printing it, and the comparison has to do the same rounding
    to land on the same text. A positive setting or no setting at all means psql printed the
    shortest round-trip representation, same as this build's own float formatting, so nothing
    changes.
    """
    if value is None:
        return None
    if slt_type == "I":
        return int(value)
    if slt_type == "R":
        value = float(value)
        if float_digits is not None and float_digits <= 0:
            value = float("%.{}g".format(15 + float_digits) % value)
        return value
    if isinstance(value, bool):
        return value
    return str(value)


def _diff(table: pgparse.AlignedTable, result: duckdbcli.QueryResult, directive: str,
          float_digits: Optional[int] = None) -> str:
    """A unified diff of upstream's rows against this build's, both brought onto one footing."""
    expected = [[coerce(cell, t) for cell, t in zip(row, directive)] for row in table.rows]
    actual = [[_actual(v, t, float_digits) for v, t in zip(row, directive)] for row in result.rows]
    return "\n".join(
        difflib.unified_diff(
            [repr(r) for r in expected],
            [repr(r) for r in actual],
            fromfile="upstream",
            tofile="this build",
            lineterm="",
        )
    )


def _differing(table: pgparse.AlignedTable, result: duckdbcli.QueryResult, directive: str,
               float_digits: Optional[int] = None) -> int:
    """How many rows are not identical between upstream's table and this build's result."""
    expected = [[coerce(cell, t) for cell, t in zip(row, directive)] for row in table.rows]
    actual = [[_actual(v, t, float_digits) for v, t in zip(row, directive)] for row in result.rows]
    return sum(1 for e, a in zip_longest(expected, actual, fillvalue=object()) if e != a)


def tie_shape(columns: Sequence[str], rows: Sequence[Sequence[Any]], directive: str,
              float_digits: Optional[int] = None) -> Optional[List[Tuple[Any, Any, int, Any]]]:
    """The per-path summary upstream guarantees: endpoints, row count, total cost.

    Returns None when this result carries no route, in which case there is nothing a tie could
    hide and every difference is a defect.
    """
    lowered = [c.lower() for c in columns]
    if not all(c in lowered for c in TIE_COLUMNS):
        return None
    if not any(c in lowered for c in ROUTE_COLUMNS):
        return None
    start = lowered.index("start_vid")
    end = lowered.index("end_vid")
    agg = lowered.index("agg_cost")
    groups: Dict[Tuple[Any, Any], List[Any]] = {}
    for row in rows:
        key = (_actual(row[start], directive[start], float_digits),
               _actual(row[end], directive[end], float_digits))
        groups.setdefault(key, []).append(_actual(row[agg], directive[agg], float_digits))
    return sorted(
        (key[0], key[1], len(costs), max(costs)) for key, costs in groups.items()
    )


def classify(table: pgparse.AlignedTable, result: duckdbcli.QueryResult, directive: str,
             float_digits: Optional[int] = None) -> str:
    """"match", "tie" or "defect" for one block."""
    expected = [[coerce(cell, t) for cell, t in zip(row, directive)] for row in table.rows]
    actual = [[_actual(v, t, float_digits) for v, t in zip(row, directive)] for row in result.rows]
    if expected == actual:
        return "match"
    upstream_shape = tie_shape(table.columns, table.rows, directive, float_digits)
    ours_shape = tie_shape(result.columns, result.rows, directive, float_digits)
    if upstream_shape is None or ours_shape is None:
        return "defect"
    return "tie" if upstream_shape == ours_shape else "defect"


def check_column_count(category: str, stem: str, block_name: str,
                        table: pgparse.AlignedTable, result: duckdbcli.QueryResult) -> None:
    """Guard every consumer of ``directive``, which is derived from this build's columns alone.

    ``classify``/``_diff``/``_differing``/``expected_cells`` all build rows with
    ``zip(row, directive)``; ``zip`` silently truncates to the shorter side. Without this guard a
    dropped or added output column on either side would be dropped from the comparison instead of
    failing it, in exactly the direction this generator exists to catch.
    """
    if len(table.columns) != len(result.columns):
        raise Mismatch(
            "{}/{}.pg {}: upstream returns {} columns, this build {}".format(
                category, stem, block_name, len(table.columns), len(result.columns)))


def companion_sql(sql: str) -> str:
    """Wrap a query in the assertion that survives a tie flip."""
    return (
        "SELECT start_vid, end_vid, count(*), max(agg_cost)\n"
        "FROM ({})\n"
        "GROUP BY start_vid, end_vid ORDER BY 1, 2;".format(sql.strip().rstrip(";"))
    )


def companion_rows(table: pgparse.AlignedTable, directive: str) -> List[List[str]]:
    """The companion's expected rows, computed from upstream's table and not from this build."""
    shape = tie_shape(table.columns, table.rows, directive)
    assert shape is not None  # classify() already established this
    out = []
    for start, end, count, total in shape:
        total_text = repr(total)
        if isinstance(total, float) and total.is_integer():
            total_text = str(int(total))
        out.append([str(start), str(end), str(count), total_text])
    return out


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
        header = "# {}\n".format(item.name)
        if item.note:
            wrapped = textwrap.wrap(
                "{}: {}".format(item.name, item.note),
                width=98,
                initial_indent="# ",
                subsequent_indent="# ",
            )
            header = "\n".join(wrapped) + "\n"
        parts.append(
            "{}query {}\n{}\n----\n{}\n".format(
                header, item.directive, item.sql.strip(), rows
            )
        )
    return "".join(parts)


def expected_cells(table: pgparse.AlignedTable, directive: str) -> List[List[str]]:
    """Upstream's own cells, normalised only where sqllogictest needs it.

    A boolean-typed cell is rendered as ``true``/``false``, mirroring ``coerce()``'s reading of
    psql's ``t``/``f`` and the ``T`` directive the column gets: DuckDB itself prints a BOOLEAN as
    ``true``/``false``, and AGENTS.md documents that as the convention a boolean column follows.
    An "I"/"R" cell is stripped in full here, same as in ``coerce()``, because
    pgparse.parse_aligned deliberately leaves a right-aligned number's own extra left padding in
    place; a plain "T" cell is written out exactly as parse_aligned produced it (DuckDB's
    sqllogictest runner splits an expected row on tabs without trimming, so a leading space
    written into the generated file here survives), which is what lets a value like ' visits'
    round-trip into the emitted test.
    """
    out = []
    for row in table.rows:
        cells = []
        for cell, slt_type in zip(row, directive):
            stripped = cell.strip()
            if stripped == "":
                cells.append("NULL")
            elif slt_type == "T" and stripped in ("t", "f"):
                cells.append("true" if stripped == "t" else "false")
            elif slt_type == "T":
                cells.append(cell)
            else:
                cells.append(stripped)
        out.append(cells)
    return out


def process(category: str, stem: str, db: duckdbcli.DuckDB, implemented: Dict[str, str],
            skips: Dict[str, Dict[str, str]], ties: Dict[str, Dict[str, dict]]) -> List[Item]:
    root = pathlib.Path(DOCQUERIES) / category
    pg_text = (root / (stem + ".pg")).read_text()
    pg_blocks = pgparse.nonempty(pgparse.split_blocks(pg_text))
    float_digits = pgparse.extra_float_digits(pg_text)
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
        check_column_count(category, stem, block.name, table, result)
        directive = slt_types(result.types)
        if any(t == "T" and cell.strip() == "" for row in table.rows for cell, t in zip(row, directive)):
            items.append(Skipped(block.name, "blank cell in a text column"))
            continue
        verdict = classify(table, result, directive, float_digits)
        if verdict == "defect":
            raise Mismatch(
                "{}/{}.pg {}: this build's answer is not an equal-cost alternative to "
                "upstream's\n{}".format(
                    category, stem, block.name, _diff(table, result, directive, float_digits)
                )
            )
        if verdict == "tie":
            ties.setdefault("{}/{}.pg".format(category, stem), {})[block.name] = {
                "reason": "equal-cost tie",
                "upstream_rows": table.row_count,
                "differing_rows": _differing(table, result, directive, float_digits),
            }
            items.append(
                Emitted(
                    block.name,
                    "IIIR",
                    companion_sql(sql),
                    companion_rows(table, directive),
                    note=(
                        "this build reaches the same optimum by a different equal-cost route, "
                        "so only what upstream guarantees is asserted: endpoints, row count and "
                        "total agg_cost"
                    ),
                )
            )
            continue
        items.append(Emitted(block.name, directive, sql, expected_cells(table, directive)))
    return items


def select_stems(root: pathlib.Path, implemented: Dict[str, str],
                 only: Optional[Sequence[str]]) -> List[pathlib.Path]:
    """The .pg files a run processes: each implemented function's own documentation page.

    Upstream's documentation also calls implemented functions as helpers on other pages (for
    example contraction.pg calls pgr_dijkstra), mostly against tables an earlier block of that page
    created, which this generator never executes. Selecting by stem keeps exactly the pages
    written about a function this build provides.
    """
    selected = []
    for pg in sorted(root.glob("*/*.pg")):
        if only and pg.parent.name not in only:
            continue
        if ("pgr_" + pg.stem).lower() not in implemented:
            continue
        if not (pg.parent / (pg.stem + ".result")).exists():
            continue
        selected.append(pg)
    return selected


def stale_outputs(existing: Sequence[str], rendered: Sequence[str],
                  only: Optional[Sequence[str]]) -> List[str]:
    """Generated files this run did not produce; a scoped run only judges its own categories."""
    produced = set(rendered)
    stale = []
    for path in existing:
        category = pathlib.PurePosixPath(path).parts[-2]
        if only and category not in only:
            continue
        if path not in produced:
            stale.append(path)
    return sorted(stale)


def merge_ties(existing: Dict[str, Dict[str, dict]], fresh: Dict[str, Dict[str, dict]],
               processed: Set[str]) -> Dict[str, Dict[str, dict]]:
    """Replace the ties of every stem this run processed; keep every other stem's verbatim.

    A scoped run (--category) must not erase what another stem recorded, and a processed stem
    whose blocks are no longer ties must lose its entry.
    """
    merged = {key: value for key, value in existing.items() if key not in processed}
    merged.update(fresh)
    return merged


def generate(
    db: duckdbcli.DuckDB, only: Optional[Sequence[str]]
) -> Tuple[Dict[str, str], Dict[str, Dict[str, dict]], Set[str]]:
    """Render every selected docqueries file, returning ({output path: file body}, ties, processed stems)."""
    implemented = implemented_names(db)
    skips = json.loads(pathlib.Path(SKIP_FILE).read_text())
    rendered: Dict[str, str] = {}
    ties: Dict[str, Dict[str, dict]] = {}
    processed: Set[str] = set()
    for pg in select_stems(pathlib.Path(DOCQUERIES), implemented, only):
        category, stem = pg.parent.name, pg.stem
        processed.add("{}/{}.pg".format(category, stem))
        items = process(category, stem, db, implemented, skips, ties)
        if not any(isinstance(item, Emitted) for item in items):
            continue
        rendered["{}/{}/{}.test".format(OUT_ROOT, category, stem)] = render(category, stem, items)
    return rendered, ties, processed


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
    parser.add_argument("--category", action="append", default=None,
                        help="restrict the run to this docqueries category (repeatable; for debugging)")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)

    db = duckdbcli.DuckDB(args.duckdb, preamble=_raw_preamble())
    rendered, fresh_ties, processed = generate(db, args.category)
    ties_path = pathlib.Path(TIES_FILE)
    existing_ties = json.loads(ties_path.read_text()) if ties_path.exists() else {}
    ties = merge_ties(existing_ties, fresh_ties, processed)
    rendered[TIES_FILE] = json.dumps(ties, indent=2, sort_keys=True) + "\n"

    existing = sorted(p.as_posix() for p in pathlib.Path(OUT_ROOT).glob("*/*.test"))
    stale = stale_outputs(existing, [p for p in rendered if p != TIES_FILE], args.category)

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
    for path in stale:
        if args.check:
            failures += 1
            print("{}: no longer generated (its stem is not selected or emits nothing)".format(path))
        else:
            pathlib.Path(path).unlink()
            print("removed {}".format(path))
    if args.check and failures:
        print("\n{} generated file(s) are stale".format(failures), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
