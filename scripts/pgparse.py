# SPDX-License-Identifier: GPL-2.0-or-later
"""Parsers for pgRouting's committed test fixtures.

Two upstream formats are read here and nowhere else:

* a ``.pg`` file -- SQL split into named blocks by ``/* -- <name> */`` markers;
* a ``.result`` file -- the psql transcript of running that ``.pg``, carrying the same markers
  plus psql's aligned output tables.

Nothing here touches DuckDB or the filesystem, so it is unit-tested directly.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

# Upstream is not consistent about the spaces inside a marker: dijkstra.pg writes both
# "/* -- q15 */" and "/* -- q151*/". A pattern that requires the trailing space silently merges
# four blocks into their predecessor.
MARKER_RE = re.compile(r"/\*\s*--\s*([A-Za-z][\w]*(?:-[\w]+)*)\s*\*/")

_SEPARATOR_RE = re.compile(r"^-+(?:\+-+)*$")
_ROWCOUNT_RE = re.compile(r"^\((\d+) rows?\)$")
_EXTRA_FLOAT_DIGITS_RE = re.compile(r"(?i)\bSET\s+extra_float_digits\s*=\s*(-?\d+)\s*;")


@dataclass
class Block:
    name: str
    sql: str


@dataclass
class AlignedTable:
    columns: List[str]
    rows: List[List[str]]
    row_count: int


@dataclass
class ResultBlock:
    name: str
    tables: List[AlignedTable] = field(default_factory=list)
    notices: List[str] = field(default_factory=list)
    error: Optional[str] = None


def split_blocks(text: str) -> List[Block]:
    """Split a .pg or .result body on its markers, dropping the text before the first one."""
    parts = MARKER_RE.split(text)
    return [Block(name, body) for name, body in zip(parts[1::2], parts[2::2])]


def nonempty(blocks: List[Block]) -> List[Block]:
    """Drop the markers upstream leaves empty as documentation anchors."""
    return [b for b in blocks if b.sql.strip()]


def extra_float_digits(text: str) -> Optional[int]:
    """The value of a ``SET extra_float_digits = <int>;`` statement in a .pg file's preamble.

    PostgreSQL's ``float8out`` prints ``DBL_DIG + extra_float_digits`` significant digits when
    that setting is not positive, so this session setting is why a handful of pages' committed
    transcripts show a clean ``0.3`` for a value a plain IEEE double computes as
    ``0.30000000000000004``. Only the text split_blocks drops -- everything before the first
    ``/* -- <name> */`` marker -- is read; a page that sets it again inside a named block is
    setting it for that block alone, deliberately not the page's documented default, so that
    occurrence is not read here.
    """
    preamble = MARKER_RE.split(text, maxsplit=1)[0]
    match = _EXTRA_FLOAT_DIGITS_RE.search(preamble)
    return int(match.group(1)) if match else None


def parse_aligned(lines: List[str], i: int) -> Tuple[Optional[AlignedTable], int]:
    """Read one psql aligned table starting at ``lines[i]``.

    Column boundaries come from the ``---+---`` separator rather than from the header, so a value
    containing a ``|`` is sliced correctly. Returns ``(None, i)`` when this is not a table.
    """
    if i + 1 >= len(lines) or not _SEPARATOR_RE.match(lines[i + 1].strip()):
        return None, i
    separator = lines[i + 1]
    spans: List[Tuple[int, Optional[int]]] = []
    start = 0
    for match in re.finditer(r"\+", separator):
        spans.append((start, match.start()))
        start = match.start() + 1
    # The last column runs to end of line: psql pads the separator to the widest cell, but a data
    # row may be padded differently.
    spans.append((start, None))

    def slice_cells(line: str) -> List[str]:
        return [line[a:b] for a, b in spans]

    # The header names a column; psql centers it in the column width regardless of how the data
    # below is aligned, so it carries no data to lose and is stripped in full.
    columns = [cell.strip() for cell in slice_cells(lines[i])]

    def cut(line: str) -> List[str]:
        cells = []
        for raw in slice_cells(line):
            # psql's aligned format writes exactly one left margin space before every data cell,
            # whichever alignment its column uses, and then the value: text is left-aligned and
            # right-padded to the column's width, a number is right-aligned and left-padded.
            # Dropping only that one margin space -- never every leading space -- plus all
            # trailing whitespace (always padding, never data, on either alignment) is what lets a
            # text value that genuinely starts with a space round-trip (pgRouting's withPoints.pg
            # q7 builds sentences this way, e.g. returning ' visits'). A right-aligned number still
            # carries its own extra left padding after this; coerce() strips the rest of it because
            # an "I"/"R" column can never have a genuine leading space. What this cannot recover: a
            # text value's own trailing space is indistinguishable from psql's own padding, so it
            # is lost the same way it always was.
            trimmed = raw[1:] if raw.startswith(" ") else raw
            cells.append(trimmed.rstrip())
        return cells

    rows: List[List[str]] = []
    j = i + 2
    while j < len(lines):
        match = _ROWCOUNT_RE.match(lines[j].strip())
        if match:
            return AlignedTable(columns, rows, int(match.group(1))), j + 1
        rows.append(cut(lines[j]))
        j += 1
    # No "(N rows)" terminator: this was not a table after all.
    return None, i


def parse_result_block(name: str, body: str) -> ResultBlock:
    """Pull the tables, notices and error out of one .result block."""
    lines = body.splitlines()
    block = ResultBlock(name=name)
    i = 0
    while i < len(lines):
        stripped = lines[i].strip()
        if stripped.startswith("NOTICE:"):
            block.notices.append(stripped[len("NOTICE:"):].strip())
            i += 1
            continue
        if stripped.startswith("ERROR:"):
            block.error = stripped[len("ERROR:"):].strip()
            i += 1
            continue
        table, nxt = parse_aligned(lines, i)
        if table is not None:
            block.tables.append(table)
            i = nxt
            continue
        i += 1
    return block
