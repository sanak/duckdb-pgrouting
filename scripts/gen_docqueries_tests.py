# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate sqllogictests from pgRouting's documentation queries.

For every ``/* -- qN */`` block of an upstream ``.pg`` file this tool runs the query, verbatim,
against the built duckdb binary -- the extension's public names are upstream's own ``pgr_``
names -- and compares the answer with upstream's committed ``.result`` transcript. A block that
agrees is emitted with upstream's own expected rows; a block that disagrees stops the generator.
A block that differs only by an equal-cost route is downgraded to a tie-insensitive assertion and
recorded in test/pgrouting_ties.json.

Only each implemented function's own documentation page is processed (see select_stems);
--category narrows that further for debugging.

Which upstream functions are implemented is never written down here: it is read back from the
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

# Driving-distance results: which predecessor reaches a vertex is an equal-cost choice, but each
# root's set of reached vertices and their costs are not.
TREE_TIE_FUNCTIONS = frozenset({"pgr_drivingdistance", "pgr_withpointsdd"})
TREE_TIE_COLUMNS = ("start_vid", "node", "agg_cost")

# K-shortest-path results (pgr_turnRestrictedPath runs Yen's algorithm too): which K of several
# equal-cost paths come back depends on the order in which Yen's algorithm explores them, and the
# per-endpoint row count and maximum cost that settle a route tie cannot see that choice. A block
# calling one of these that differs from upstream stays a defect until a person decides an
# invariant for it. pgr_edgeDisjointPaths is here for the same reason: which set of disjoint paths
# comes back is not a cost tie, and a differing block stays a defect.
KSP_FUNCTIONS = frozenset({"pgr_ksp", "pgr_withpointsksp", "pgr_turnrestrictedpath", "pgr_edgedisjointpaths"})

# Maximum-flow results: which edges carry the flow depends on the algorithm's search order and so on
# the order the edges are read in; the flow's value does not, nor, for a minimum-cost flow, its total
# cost. A block calling one of these whose rows differ from upstream's is a flow tie when both totals
# match, and is then asserted through them.
FLOW_FUNCTIONS = frozenset({"pgr_pushrelabel", "pgr_boykovkolmogorov", "pgr_edmondskarp", "pgr_maxflowmincost"})
FLOW_NOTE = (
    "which edges carry the flow depends on the order the edges are read in, so only what every maximum "
    "flow shares is asserted: its value (the net flow leaving the sources) and, for a minimum-cost flow, "
    "its total cost"
)

# Kruskal/Prim results: which minimum spanning forest (or which walk of one) comes back among
# equal-cost edges is never guaranteed, unlike a route or a driving-distance tree. Boost's
# kruskal_minimum_spanning_tree pops equal-weight edges from a std::priority_queue whose tie order
# depends on the C++ standard library; boost::prim_minimum_spanning_tree ties on its own heap
# instead. Every block calling one of these is a forest companion unconditionally, whether or not
# this build's raw answer happens to equal upstream's.
FOREST_FUNCTIONS = frozenset({
    "pgr_kruskal", "pgr_kruskalbfs", "pgr_kruskaldfs", "pgr_kruskaldd",
    "pgr_prim", "pgr_primbfs", "pgr_primdfs", "pgr_primdd",
})
FOREST_EDGE_COLUMNS = ("edge", "cost")
FOREST_TREE_COLUMNS = ("seq", "depth", "start_vid", "pred", "node", "edge", "cost", "agg_cost")

# Bandwidth-reducing vertex orderings. Boost's cuthill_mckee_ordering sorts each level of its search
# with std::sort, king_ordering keeps its queue with std::make_heap and sloan_ordering picks its end
# points through a std::priority_queue, so which of several vertices of equal degree comes first
# depends on the C++ standard library (libstdc++, libc++ and MSVC's differ) as well as on the order
# the edges are read in. Every block calling one of these is an ordering companion unconditionally,
# as a spanning-forest block is, whether or not this build's raw answer happens to equal upstream's.
ORDERING_FUNCTIONS = frozenset({"pgr_cuthillmckeeordering", "pgr_kingordering", "pgr_sloanordering"})
ORDERING_COLUMNS = ("seq", "node")
ORDERING_DIRECTIVE = "IIII"
ORDERING_NOTE = (
    "which of several vertices of equal degree comes first depends on the edge order and on the C++ "
    "standard library's tie-breaks, so only what every such ordering shares is asserted: its row "
    "count, how many distinct vertices it lists, and its first and last seq"
)

# A call is an upstream function only when pgr_ starts an identifier, so my_pgr_dijkstra_helper
# is left alone.
CALL_RE = re.compile(r"(?<![A-Za-z0-9_])(pgr_\w+)\s*\(", re.IGNORECASE)

# PostGIS's ST_AsText writes "LINESTRING(1.8 0.4,2 0.4)"; duckdb-spatial writes
# "LINESTRING (1.8 0.4, 2 0.4)". Only the spacing differs, so an upstream WKT cell is respelled in
# DuckDB's form before it is compared or emitted; the coordinates are left exactly as upstream
# printed them.
_WKT_START_RE = re.compile(r"^([A-Z]+)\(")
_WKT_TYPES = {
    "POINT", "LINESTRING", "POLYGON", "MULTIPOINT", "MULTILINESTRING", "MULTIPOLYGON",
    "GEOMETRYCOLLECTION",
}

# What a generated page whose function needs duckdb-spatial runs first (see prelude()).
SPATIAL_SQL = "INSTALL spatial;\nLOAD spatial;\n"

# Set to 1 where duckdb-spatial cannot be installed (Checks.yml sets it on the next line, whose
# unreleased DuckDB has no published spatial binary). The spatial pages are then neither
# regenerated nor compared; their committed files are kept as they are.
NO_SPATIAL_ENV = "PGROUTING_NO_SPATIAL"


def spatial_disabled() -> bool:
    return os.environ.get(NO_SPATIAL_ENV, "") == "1"


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


def implemented_names(db: duckdbcli.DuckDB) -> Set[str]:
    """Lowercase upstream names of the implemented functions, straight from the catalog tag.

    Lowercase because the documentation's casing varies (pgr_dijkstraCost, pgr_dijkstracost) and
    both PostgreSQL and DuckDB resolve a function name case-insensitively.
    """
    result = db.query(
        "SELECT DISTINCT lower(tags['pgrouting_name']) AS upstream "
        "FROM duckdb_functions() "
        "WHERE tags['ext'] = 'pgrouting' AND tags['pgrouting_name'] IS NOT NULL"
    )
    return {row[0] for row in result.rows}


def translate(sql: str, implemented: Set[str]) -> Tuple[str, List[str]]:
    """The query as this build runs it, and the upstream functions it calls that are missing.

    The public names are upstream's own, so an implemented call passes through verbatim, casing
    included: the rewrite is the identity. What remains of this step is the membership test that
    lets process() skip a block calling a function this build does not provide.
    """
    missing = [match.group(1) for match in CALL_RE.finditer(sql)
               if match.group(1).lower() not in implemented]
    return sql, missing


def duckdb_wkt(cell: str) -> str:
    """An upstream WKT cell in duckdb-spatial's spelling; any other text unchanged."""
    match = _WKT_START_RE.match(cell)
    if match is None or match.group(1) not in _WKT_TYPES:
        return cell
    body = cell[len(match.group(1)):]
    return match.group(1) + " " + re.sub(r",(?! )", ", ", body)


_PG_ARRAY_RE = re.compile(r"^\{(.*)\}$")


def pg_array_cell(cell: str) -> str:
    """An upstream integer-array cell ({12,17,16}) in DuckDB's list spelling ([12, 17, 16])."""
    match = _PG_ARRAY_RE.match(cell.strip())
    if match is None:
        return cell
    body = match.group(1).strip()
    if not body:
        return "[]"
    return "[" + ", ".join(part.strip() for part in body.split(",")) + "]"


def respell_list_cells(table: pgparse.AlignedTable, duck_types: Sequence[str]) -> pgparse.AlignedTable:
    """Upstream's table with every cell of an integer-list column in DuckDB's spelling.

    psql prints a BIGINT[] as {4,7}; DuckDB's CLI (read as JSON) and its sqllogictest runner print
    [4, 7]. Only integer lists are respelled: an element of any other type would need its own
    quoting rules, and no documentation query returns one. A blank cell (NULL) stays blank.
    """
    columns = {
        index for index, duck_type in enumerate(duck_types)
        if duck_type.endswith("[]") and duck_type[:-2].strip().upper() in _INTEGER_TYPES
    }
    if not columns:
        return table
    rows = [
        [pg_array_cell(cell) if index in columns and cell.strip() else cell for index, cell in enumerate(row)]
        for row in table.rows
    ]
    return pgparse.AlignedTable(table.columns, rows, table.row_count)


def spatial_names(db: duckdbcli.DuckDB) -> Set[str]:
    """Lowercase upstream names of the functions whose queries need duckdb-spatial loaded.

    Read from the pgrouting_requires catalog tag, like implemented_names() reads pgrouting_name.
    """
    result = db.query(
        "SELECT DISTINCT lower(tags['pgrouting_name']) AS upstream "
        "FROM duckdb_functions() "
        "WHERE tags['ext'] = 'pgrouting' AND tags['pgrouting_requires'] = 'spatial'"
    )
    return {row[0] for row in result.rows}


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
    parse_aligned produced it, so a value like ' visits' round-trips. A WKT geometry is respelled
    as duckdb-spatial writes it (duckdb_wkt).
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
    return duckdb_wkt(cell)


def _actual(value: Any, slt_type: str, float_digits: Optional[int] = None) -> Any:
    """One value from DuckDB, brought onto the same footing as a coerced upstream cell.

    An integer list arrives from the CLI's JSON as a Python list, whose str() is DuckDB's own
    spelling ([4, 7]).

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


def tree_tie_shape(sql: str, columns: Sequence[str], rows: Sequence[Sequence[Any]], directive: str,
                   float_digits: Optional[int] = None) -> Optional[List[Tuple[Any, ...]]]:
    """Each root's reached vertices with their costs, for a driving-distance query.

    None for every other query: a spanning tree or a traversal that edge order picks has no
    invariant here, so any difference in it stays a defect.
    """
    called = {match.group(1).lower() for match in CALL_RE.finditer(sql)}
    if not called & TREE_TIE_FUNCTIONS:
        return None
    lowered = [c.lower() for c in columns]
    if not all(c in lowered for c in TREE_TIE_COLUMNS):
        return None
    index = [lowered.index(c) for c in TREE_TIE_COLUMNS]
    return sorted(tuple(_actual(row[i], directive[i], float_digits) for i in index) for row in rows)


def flow_ends(columns: Sequence[str]) -> Optional[Tuple[str, str]]:
    """The tail and head column names of a flow result, or None when it is not one."""
    lowered = [c.lower() for c in columns]
    if "flow" not in lowered:
        return None
    for tail, head in (("start_vid", "end_vid"), ("source", "target")):
        if tail in lowered and head in lowered:
            return tail, head
    return None


def flow_shape(columns: Sequence[str], rows: Sequence[Sequence[Any]], directive: str,
               float_digits: Optional[int] = None) -> Optional[Tuple[Any, Any]]:
    """A flow's value and, with a cost column, its total cost; None when the result is no flow.

    The value is the sum of every vertex's positive net outflow: inner vertices balance and the
    targets only take in, so that is what the sources send, without knowing which they are.
    """
    ends = flow_ends(columns)
    if ends is None:
        return None
    lowered = [c.lower() for c in columns]
    tail, head, flow = (lowered.index(ends[0]), lowered.index(ends[1]), lowered.index("flow"))
    net: Dict[Any, int] = {}
    for row in rows:
        amount = _actual(row[flow], directive[flow], float_digits)
        net_tail = _actual(row[tail], directive[tail], float_digits)
        net_head = _actual(row[head], directive[head], float_digits)
        net[net_tail] = net.get(net_tail, 0) + amount
        net[net_head] = net.get(net_head, 0) - amount
    value = sum(o for o in net.values() if o > 0)
    total_cost = None
    if "cost" in lowered:
        cost = lowered.index("cost")
        total_cost = sum(_actual(row[cost], directive[cost], float_digits) for row in rows)
    return value, total_cost


def classify(table: pgparse.AlignedTable, result: duckdbcli.QueryResult, directive: str,
             float_digits: Optional[int] = None, sql: str = "") -> str:
    """"match", "tie", "tree_tie", "flow_tie" or "defect" for one block."""
    expected = [[coerce(cell, t) for cell, t in zip(row, directive)] for row in table.rows]
    actual = [[_actual(v, t, float_digits) for v, t in zip(row, directive)] for row in result.rows]
    if expected == actual:
        return "match"
    called = {match.group(1).lower() for match in CALL_RE.finditer(sql)}
    if called & KSP_FUNCTIONS:
        return "defect"
    if called & FLOW_FUNCTIONS:
        upstream_flow = flow_shape(table.columns, table.rows, directive, float_digits)
        ours_flow = flow_shape(result.columns, result.rows, directive, float_digits)
        if upstream_flow is not None and upstream_flow == ours_flow:
            return "flow_tie"
        return "defect"
    upstream_shape = tie_shape(table.columns, table.rows, directive, float_digits)
    ours_shape = tie_shape(result.columns, result.rows, directive, float_digits)
    if upstream_shape is not None and ours_shape is not None:
        return "tie" if upstream_shape == ours_shape else "defect"
    upstream_tree = tree_tie_shape(sql, table.columns, table.rows, directive, float_digits)
    ours_tree = tree_tie_shape(sql, result.columns, result.rows, directive, float_digits)
    if upstream_tree is not None and ours_tree is not None:
        return "tree_tie" if upstream_tree == ours_tree else "defect"
    return "defect"


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


def tree_companion_sql(sql: str) -> str:
    """Wrap a driving-distance query in the assertion that survives a predecessor tie."""
    return "SELECT start_vid, node, agg_cost\nFROM ({})\nORDER BY 1, 2, 3;".format(sql.strip().rstrip(";"))


def tree_companion_rows(table: pgparse.AlignedTable, directive: str, sql: str) -> List[List[str]]:
    """The tree companion's expected rows, computed from upstream's table."""
    shape = tree_tie_shape(sql, table.columns, table.rows, directive)
    assert shape is not None  # classify() already established this
    out = []
    for start, node, cost in shape:
        cost_text = str(int(cost)) if isinstance(cost, float) and cost.is_integer() else repr(cost)
        out.append([str(start), str(node), cost_text])
    return out


def flow_companion_sql(sql: str, columns: Sequence[str]) -> str:
    """Wrap a flow query in the assertion that survives another flow of the same value."""
    ends = flow_ends(columns)
    assert ends is not None  # classify() already established this
    total_cost = ", (SELECT sum(cost) FROM r)" if "cost" in [c.lower() for c in columns] else ""
    return (
        "WITH r AS ({}),\n"
        "n AS (SELECT v, sum(f) AS o FROM (SELECT {} AS v, flow AS f FROM r UNION ALL SELECT {}, -flow FROM r) "
        "GROUP BY v)\n"
        "SELECT (SELECT sum(o) FROM n WHERE o > 0){};".format(sql.strip().rstrip(";"), ends[0], ends[1], total_cost)
    )


def flow_directive(columns: Sequence[str]) -> str:
    """The companion's column types: the value, and the total cost when there is a cost column."""
    return "IR" if "cost" in [c.lower() for c in columns] else "I"


def flow_companion_rows(table: pgparse.AlignedTable, directive: str) -> List[List[str]]:
    """The flow companion's expected row, computed from upstream's table, not from this build."""
    shape = flow_shape(table.columns, table.rows, directive)
    assert shape is not None  # classify() already established this
    value, total_cost = shape
    row = [str(value)]
    if total_cost is not None:
        row.append(str(int(total_cost)) if float(total_cost).is_integer() else repr(total_cost))
    return [row]


def is_forest_call(sql: str) -> bool:
    """Whether sql calls a spanning-forest function (pgr_kruskal*/pgr_prim*), matched like CALL_RE."""
    called = {match.group(1).lower() for match in CALL_RE.finditer(sql)}
    return bool(called & FOREST_FUNCTIONS)


def forest_shape(columns: Sequence[str]) -> Optional[str]:
    """"edges" (pgr_kruskal/pgr_prim), "tree" (the *BFS/*DFS/*DD families), or None.

    None is a shape this rule does not recognise: process() raises Mismatch on it, since a forest
    function returning neither known column set is a defect this generator cannot classify.
    """
    lowered = {c.lower() for c in columns}
    if lowered == set(FOREST_EDGE_COLUMNS):
        return "edges"
    if lowered == set(FOREST_TREE_COLUMNS):
        return "tree"
    return None


# The *BFS/*DFS forms without a max_depth: an unlimited walk always reaches its whole connected
# component, whichever equal-cost forest was built, so its row count per root is an invariant too.
# *BFS/*DFS with a max_depth, and the DD forms (which always take a required distance bound), stop
# at a boundary that itself depends on which forest was built, so they keep the four-column
# companion; see forest_variant().
FOREST_UNLIMITED_FUNCTIONS = frozenset({
    "pgr_kruskalbfs", "pgr_kruskaldfs", "pgr_primbfs", "pgr_primdfs",
})


def _matching_paren(sql: str, open_index: int) -> int:
    """Index of the ')' matching the '(' at open_index, skipping quoted text and nested parens.

    A single quote doubled ('') is psql/PostgreSQL's escape for a literal quote inside a quoted
    string, so it does not end the quote.
    """
    depth = 1
    in_quote = False
    i = open_index + 1
    n = len(sql)
    while i < n:
        ch = sql[i]
        if in_quote:
            if ch == "'":
                if i + 1 < n and sql[i + 1] == "'":
                    i += 2
                    continue
                in_quote = False
            i += 1
            continue
        if ch == "'":
            in_quote = True
        elif ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return n


def top_level_arg_count(args: str) -> int:
    """How many comma-separated top-level arguments a call's raw argument-list text has.

    A comma inside a quoted string (an inner SQL query may itself call other functions, with
    their own commas and parentheses) or inside an ``ARRAY[...]``/``[...]`` literal is not a
    top-level separator; only a comma outside all of those, at paren/bracket depth 0, separates
    two arguments of the outer call.
    """
    if not args.strip():
        return 0
    depth = 0
    in_quote = False
    commas = 0
    i = 0
    n = len(args)
    while i < n:
        ch = args[i]
        if in_quote:
            if ch == "'":
                if i + 1 < n and args[i + 1] == "'":
                    i += 2
                    continue
                in_quote = False
            i += 1
            continue
        if ch == "'":
            in_quote = True
        elif ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        elif ch == "," and depth == 0:
            commas += 1
        i += 1
    return commas + 1


def is_unlimited_forest_call(sql: str) -> bool:
    """Whether sql's forest call is an unlimited *BFS/*DFS walk (see FOREST_UNLIMITED_FUNCTIONS).

    True only for one of those functions called with exactly its two required top-level
    arguments (the edges SQL and a root or a roots array) -- no max_depth, positional or named.
    """
    for match in CALL_RE.finditer(sql):
        if match.group(1).lower() not in FOREST_UNLIMITED_FUNCTIONS:
            continue
        open_index = match.end() - 1  # CALL_RE's own match already consumes the '('.
        close_index = _matching_paren(sql, open_index)
        return top_level_arg_count(sql[open_index + 1:close_index]) == 2
    return False


def forest_variant(shape: str, sql: str) -> str:
    """"edges", "tree" or "tree_unlimited" (a "tree" whose call is_unlimited_forest_call)."""
    if shape == "tree" and is_unlimited_forest_call(sql):
        return "tree_unlimited"
    return shape


FOREST_DIRECTIVES = {"edges": "IR", "tree": "IITI", "tree_unlimited": "IITII"}

FOREST_NOTES = {
    "edges": (
        "which minimum spanning forest is built among equal-cost edges is an implementation "
        "tie-break (Kruskal's follows the C++ standard library's priority-queue order), so only "
        "what every such forest guarantees is asserted: edge count and total cost"
    ),
    "tree": (
        "which minimum spanning forest is built among equal-cost edges is an implementation "
        "tie-break (Kruskal's follows the C++ standard library's priority-queue order), so only "
        "what every such forest guarantees is asserted: each root's single depth-0 row, no "
        "repeated node, and every other row hanging off its predecessor one level up at the "
        "predecessor's cost plus its own"
    ),
    "tree_unlimited": (
        "which minimum spanning forest is built among equal-cost edges is an implementation "
        "tie-break (Kruskal's follows the C++ standard library's priority-queue order), so only "
        "what every such forest guarantees is asserted: each root's single depth-0 row, no "
        "repeated node, every other row hanging off its predecessor one level up at the "
        "predecessor's cost plus its own, and its total row count -- an unlimited walk always "
        "reaches its whole connected component, whichever equal-cost forest was built"
    ),
}


def forest_companion_sql(sql: str, variant: str) -> str:
    """Wrap a spanning-forest query in the assertion that survives any equal-cost forest choice."""
    body = sql.strip().rstrip(";")
    if variant == "edges":
        return "SELECT count(*), coalesce(sum(cost), 0)\nFROM ({});".format(body)
    extra = ",\n       count(*)" if variant == "tree_unlimited" else ""
    return (
        "WITH q AS ({})\n"
        "SELECT start_vid,\n"
        "       count(*) FILTER (WHERE depth = 0),\n"
        "       count(*) = count(DISTINCT node),\n"
        "       count(*) FILTER (WHERE depth > 0 AND NOT EXISTS (\n"
        "         SELECT 1 FROM q AS p\n"
        "         WHERE p.start_vid = q.start_vid AND p.node = q.pred AND p.depth = q.depth - 1\n"
        "           AND abs(p.agg_cost + q.cost - q.agg_cost) < 1e-9)){extra}\n"
        "FROM q\n"
        "GROUP BY start_vid\n"
        "ORDER BY start_vid;"
    ).format(body, extra=extra)


def _forest_edges_rows(table: pgparse.AlignedTable, directive: str) -> List[List[str]]:
    lowered = [c.lower() for c in table.columns]
    cost_index = lowered.index("cost")
    total = 0
    for row in table.rows:
        total += coerce(row[cost_index], directive[cost_index])
    total_text = repr(total)
    if isinstance(total, float) and total.is_integer():
        total_text = str(int(total))
    return [[str(table.row_count), total_text]]


def _forest_tree_rows(table: pgparse.AlignedTable, directive: str,
                      include_count: bool = False) -> List[List[str]]:
    lowered = [c.lower() for c in table.columns]
    index = {name: lowered.index(name) for name in FOREST_TREE_COLUMNS}
    parsed = [
        {name: coerce(row[i], directive[i]) for name, i in index.items()}
        for row in table.rows
    ]
    by_root: Dict[Any, List[Dict[str, Any]]] = {}
    for row in parsed:
        by_root.setdefault(row["start_vid"], []).append(row)
    out = []
    for start in sorted(by_root):
        rows = by_root[start]
        depth0 = sum(1 for r in rows if r["depth"] == 0)
        nodes = [r["node"] for r in rows]
        no_repeat = len(nodes) == len(set(nodes))
        by_node_depth = {(r["node"], r["depth"]): r for r in rows}
        orphans = 0
        for r in rows:
            if r["depth"] <= 0:
                continue
            pred_row = by_node_depth.get((r["pred"], r["depth"] - 1))
            linked = (
                pred_row is not None
                and abs(pred_row["agg_cost"] + r["cost"] - r["agg_cost"]) < 1e-9
            )
            if not linked:
                orphans += 1
        cells = [str(start), str(depth0), "true" if no_repeat else "false", str(orphans)]
        if include_count:
            cells.append(str(len(rows)))
        out.append(cells)
    return out


def forest_companion_rows(table: pgparse.AlignedTable, directive: str, variant: str) -> List[List[str]]:
    """The forest companion's expected rows, computed in Python from upstream's table."""
    if variant == "edges":
        return _forest_edges_rows(table, directive)
    return _forest_tree_rows(table, directive, include_count=(variant == "tree_unlimited"))


def forest_actual_rows(result: duckdbcli.QueryResult, directive: str) -> List[List[str]]:
    """This build's companion-query rows, formatted like forest_companion_rows' expected rows."""
    out = []
    for row in result.rows:
        cells = []
        for value, slt_type in zip(row, directive):
            actual = _actual(value, slt_type)
            if slt_type == "R":
                if actual is None:
                    cells.append("0")
                elif actual.is_integer():
                    cells.append(str(int(actual)))
                else:
                    cells.append(repr(actual))
            elif isinstance(actual, bool):
                cells.append("true" if actual else "false")
            else:
                cells.append(str(actual))
        out.append(cells)
    return out


def is_ordering_call(sql: str) -> bool:
    """Whether sql calls a bandwidth-reducing ordering (ORDERING_FUNCTIONS), matched like CALL_RE."""
    called = {match.group(1).lower() for match in CALL_RE.finditer(sql)}
    return bool(called & ORDERING_FUNCTIONS)


def ordering_companion_sql(sql: str) -> str:
    """Wrap an ordering query in the assertion that survives any tie-break among its vertices."""
    return "SELECT count(*), count(DISTINCT node), min(seq), max(seq)\nFROM ({});".format(
        sql.strip().rstrip(";"))


def ordering_companion_rows(table: pgparse.AlignedTable, directive: str) -> List[List[str]]:
    """The ordering companion's expected row, computed from upstream's table, not from this build."""
    lowered = [c.lower() for c in table.columns]
    seq_index = lowered.index("seq")
    node_index = lowered.index("node")
    seqs = [coerce(row[seq_index], directive[seq_index]) for row in table.rows]
    nodes = [coerce(row[node_index], directive[node_index]) for row in table.rows]
    if not seqs:
        return [["0", "0", "NULL", "NULL"]]
    return [[str(len(seqs)), str(len(set(nodes))), str(min(seqs)), str(max(seqs))]]


def ordering_actual_rows(result: duckdbcli.QueryResult) -> List[List[str]]:
    """This build's companion row, formatted like ordering_companion_rows' expected row."""
    return [["NULL" if value is None else str(int(value)) for value in row] for row in result.rows]


HEADER = """# name: {out}
# description: Generated from pgRouting's {stem} documentation queries
# group: [pgrouting]

# SPDX-License-Identifier: GPL-2.0-or-later
#
# GENERATED FILE - do not edit by hand. Every edit is lost on the next regeneration.
# Regenerate with:  GEN=ninja make release && python3 scripts/gen_docqueries_tests.py
#
# Source: {pg} and the committed transcript beside it. The queries and their expected rows are
# upstream's verbatim; the only edit is psql's aligned output, which is rewritten here as
# sqllogictest rows. A block this generator could not carry over is recorded below as a skipped
# line with its reason, so what this file does not cover is visible here rather than implied.

"""


def prelude(spatial: bool) -> str:
    """The directives between the header comment and the sample-data loader.

    A page whose function needs duckdb-spatial is tagged ``spatial``: runs that cannot install it
    skip the file through test/configs/skip_spatial.json. It then installs and loads spatial
    explicitly, because spatial is not autoloadable and ``require spatial`` only finds statically
    linked extensions.
    """
    if not spatial:
        return "require pgrouting\n\n"
    return ("tags spatial\n\nrequire pgrouting\n\n"
            "statement ok\nINSTALL spatial;\n\nstatement ok\nLOAD spatial;\n\n")


def render(category: str, stem: str, items: Sequence[Item], spatial: bool = False) -> str:
    out = "{}/{}/{}.test".format(OUT_ROOT, category, stem)
    parts = [
        HEADER.format(
            out=out,
            stem=stem,
            pg="{}/{}/{}.pg".format(DOCQUERIES, category, stem),
        ),
        prelude(spatial),
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


# psql prints a non-finite float8 as Infinity, -Infinity or NaN; DuckDB's sqllogictest runner
# compares against DuckDB's own spelling.
_NON_FINITE_CELLS = {"Infinity": "inf", "-Infinity": "-inf", "NaN": "nan"}


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
    round-trip into the emitted test. A WKT geometry cell is written in duckdb-spatial's
    spelling (duckdb_wkt), and a non-finite "R" cell in DuckDB's (``inf``); those are the only
    respellings.
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
                cells.append(duckdb_wkt(cell))
            elif slt_type == "R" and stripped in _NON_FINITE_CELLS:
                cells.append(_NON_FINITE_CELLS[stripped])
            else:
                cells.append(stripped)
        out.append(cells)
    return out


def process(category: str, stem: str, db: duckdbcli.DuckDB, implemented: Set[str],
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
        table = respell_list_cells(table, result.types)
        if is_forest_call(sql):
            shape = forest_shape(table.columns)
            if shape is None:
                raise Mismatch(
                    "{}/{}.pg {}: a spanning-forest function returned a column set the forest "
                    "invariant does not recognise: {}".format(category, stem, block.name, table.columns)
                )
            variant = forest_variant(shape, sql)
            forest_directive = FOREST_DIRECTIVES[variant]
            forest_sql = forest_companion_sql(sql, variant)
            expected_forest_rows = forest_companion_rows(table, directive, variant)
            actual_forest_rows = forest_actual_rows(db.query(forest_sql), forest_directive)
            if actual_forest_rows != expected_forest_rows:
                raise Mismatch(
                    "{}/{}.pg {}: this build's spanning forest fails the forest invariant\n"
                    "expected upstream's: {}\nthis build's:         {}".format(
                        category, stem, block.name, expected_forest_rows, actual_forest_rows
                    )
                )
            ties.setdefault("{}/{}.pg".format(category, stem), {})[block.name] = {
                "reason": "equal-cost spanning forest",
                "upstream_rows": table.row_count,
            }
            items.append(
                Emitted(
                    block.name, forest_directive, forest_sql, expected_forest_rows,
                    note=FOREST_NOTES[variant],
                )
            )
            continue
        if is_ordering_call(sql):
            if [c.lower() for c in table.columns] != list(ORDERING_COLUMNS):
                raise Mismatch(
                    "{}/{}.pg {}: an ordering function returned a column set the ordering "
                    "invariant does not recognise: {}".format(category, stem, block.name, table.columns)
                )
            ordering_sql = ordering_companion_sql(sql)
            expected_ordering_rows = ordering_companion_rows(table, directive)
            actual_ordering_rows = ordering_actual_rows(db.query(ordering_sql))
            if actual_ordering_rows != expected_ordering_rows:
                raise Mismatch(
                    "{}/{}.pg {}: this build's ordering fails the ordering invariant\n"
                    "expected upstream's: {}\nthis build's:         {}".format(
                        category, stem, block.name, expected_ordering_rows, actual_ordering_rows
                    )
                )
            ties.setdefault("{}/{}.pg".format(category, stem), {})[block.name] = {
                "reason": "vertex ordering",
                "upstream_rows": table.row_count,
            }
            items.append(
                Emitted(block.name, ORDERING_DIRECTIVE, ordering_sql, expected_ordering_rows,
                        note=ORDERING_NOTE)
            )
            continue
        if any(t == "T" and cell.strip() == "" for row in table.rows for cell, t in zip(row, directive)):
            items.append(Skipped(block.name, "blank cell in a text column"))
            continue
        verdict = classify(table, result, directive, float_digits, sql)
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
        if verdict == "tree_tie":
            ties.setdefault("{}/{}.pg".format(category, stem), {})[block.name] = {
                "reason": "equal-cost tie",
                "upstream_rows": table.row_count,
                "differing_rows": _differing(table, result, directive, float_digits),
            }
            items.append(
                Emitted(
                    block.name,
                    "IIR",
                    tree_companion_sql(sql),
                    tree_companion_rows(table, directive, sql),
                    note=(
                        "this build reaches the same vertices at the same costs through different "
                        "equal-cost predecessors, so only what upstream guarantees is asserted: "
                        "each root's vertices and their costs"
                    ),
                )
            )
            continue
        if verdict == "flow_tie":
            ties.setdefault("{}/{}.pg".format(category, stem), {})[block.name] = {
                "reason": "equal-value flow",
                "upstream_rows": table.row_count,
                "differing_rows": _differing(table, result, directive, float_digits),
            }
            items.append(
                Emitted(block.name, flow_directive(table.columns), flow_companion_sql(sql, table.columns),
                        flow_companion_rows(table, directive), note=FLOW_NOTE)
            )
            continue
        items.append(Emitted(block.name, directive, sql, expected_cells(table, directive)))
    return items


def select_stems(root: pathlib.Path, implemented: Set[str],
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
                  only: Optional[Sequence[str]], kept: Set[str] = frozenset()) -> List[str]:
    """Generated files this run did not produce; a scoped run only judges its own categories.

    A path in ``kept`` was deliberately not regenerated (see NO_SPATIAL_ENV) and is never stale.
    """
    produced = set(rendered)
    stale = []
    for path in existing:
        if path in kept:
            continue
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
) -> Tuple[Dict[str, str], Dict[str, Dict[str, dict]], Set[str], Set[str]]:
    """Render every selected docqueries file.

    Returns ({output path: file body}, ties, processed stems, kept spatial paths). A kept path is
    a spatial page this run deliberately left unregenerated and uncompared (NO_SPATIAL_ENV); see
    stale_outputs().
    """
    implemented = implemented_names(db)
    spatial = spatial_names(db)
    spatial_db = duckdbcli.DuckDB(db.binary, preamble=SPATIAL_SQL + db.preamble, flags=db.flags)
    skips = json.loads(pathlib.Path(SKIP_FILE).read_text())
    rendered: Dict[str, str] = {}
    ties: Dict[str, Dict[str, dict]] = {}
    processed: Set[str] = set()
    kept: Set[str] = set()
    for pg in select_stems(pathlib.Path(DOCQUERIES), implemented, only):
        category, stem = pg.parent.name, pg.stem
        path = "{}/{}/{}.test".format(OUT_ROOT, category, stem)
        needs_spatial = ("pgr_" + stem).lower() in spatial
        if needs_spatial and spatial_disabled():
            print("kept without checking (no spatial): {}".format(path))
            kept.add(path)
            continue
        processed.add("{}/{}.pg".format(category, stem))
        items = process(category, stem, spatial_db if needs_spatial else db, implemented, skips, ties)
        if not any(isinstance(item, Emitted) for item in items):
            continue
        rendered[path] = render(category, stem, items, spatial=needs_spatial)
    return rendered, ties, processed, kept


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
    rendered, fresh_ties, processed, kept = generate(db, args.category)
    ties_path = pathlib.Path(TIES_FILE)
    existing_ties = json.loads(ties_path.read_text()) if ties_path.exists() else {}
    ties = merge_ties(existing_ties, fresh_ties, processed)
    rendered[TIES_FILE] = json.dumps(ties, indent=2, sort_keys=True) + "\n"

    existing = sorted(p.as_posix() for p in pathlib.Path(OUT_ROOT).glob("*/*.test"))
    stale = stale_outputs(existing, [p for p in rendered if p != TIES_FILE], args.category, kept)

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
