# SPDX-License-Identifier: GPL-2.0-or-later
"""Run SQL through the built duckdb binary.

The binary produced by this repository's own build already has the pgrouting extension statically
linked, so no LOAD is needed and no Python DuckDB package is involved -- a pip-installed driver
would exercise a different build than the one CI ships.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
from dataclasses import dataclass
from typing import Any, List, Optional, Sequence, Tuple

DEFAULT_BINARY = "build/release/duckdb"

# One query against the sample fixtures takes well under a second; a hung binary must fail the
# run (and CI) instead of stalling it until the job's own limit.
TIMEOUT_SECONDS = 120

# DuckDB's JSON mode writes a non-finite DOUBLE as a bare inf, -inf or nan, which is not JSON;
# Python's json module reads Infinity, -Infinity and NaN instead. A token is rewritten only outside
# a string and only when it stands alone (not inside a word such as "info").
_NON_FINITE_RE = re.compile(r'("(?:[^"\\]|\\.)*")|(?<![\w.])(-?inf|nan)(?![\w.])')
_NON_FINITE = {"inf": "Infinity", "-inf": "-Infinity", "nan": "NaN"}


def json_with_non_finite(text: str) -> str:
    """``text`` with DuckDB's bare non-finite numbers respelled for ``json.loads``."""
    return _NON_FINITE_RE.sub(lambda m: m.group(1) or _NON_FINITE[m.group(2)], text)


class DuckDBError(RuntimeError):
    """The binary exited non-zero, or printed something that is not JSON."""


@dataclass
class QueryResult:
    columns: List[str]
    types: List[str]
    rows: List[List[Any]]


def default_binary() -> str:
    return os.environ.get("DUCKDB_BIN", DEFAULT_BINARY)


class DuckDB:
    def __init__(
        self, binary: Optional[str] = None, preamble: str = "", flags: Sequence[str] = ()
    ) -> None:
        self.binary = binary or default_binary()
        self.preamble = preamble
        # Extra command-line flags, e.g. -unsigned for a released CLI that must LOAD an unsigned
        # build. The repository's own binary never needs any: the extension is linked in.
        self.flags = list(flags)

    def _run(self, sql: str) -> List[dict]:
        script = ".mode json\n"
        if self.preamble:
            script += self.preamble.rstrip() + "\n"
        script += sql.rstrip().rstrip(";") + ";\n"
        completed = subprocess.run(
            [self.binary, *self.flags, "-batch", ":memory:"],
            input=script,
            capture_output=True,
            text=True,
            timeout=TIMEOUT_SECONDS,
        )
        if completed.returncode != 0:
            raise DuckDBError(
                "{} failed:\n{}\n{}".format(self.binary, completed.stderr.strip(), sql.strip())
            )
        # Measured: `duckdb -batch :memory:` exits 1 and writes the message to stderr when a
        # statement raises, so the returncode check above is the whole error path. stderr is
        # still surfaced below in case a future CLI warns without failing.
        if completed.stderr.strip():
            raise DuckDBError("{}\n{}".format(completed.stderr.strip(), sql.strip()))
        out = completed.stdout.strip()
        if not out:
            return []
        try:
            return json.loads(json_with_non_finite(out))
        except json.JSONDecodeError as exc:
            raise DuckDBError("not JSON: {}\n{}".format(out[:200], exc))

    def describe(self, sql: str) -> Tuple[List[str], List[str]]:
        """Column names and DuckDB logical type names for ``sql``, without running it for rows."""
        described = self._run("DESCRIBE ({})".format(sql.rstrip().rstrip(";")))
        return (
            [row["column_name"] for row in described],
            [row["column_type"] for row in described],
        )

    def query(self, sql: str) -> QueryResult:
        columns, types = self.describe(sql)
        records = self._run(sql)
        rows = [[record.get(column) for column in columns] for record in records]
        return QueryResult(columns=columns, types=types, rows=rows)

    def script(self, sql: str, timeout: float = TIMEOUT_SECONDS) -> str:
        """Run a whole script (statements and dot commands) and return what it printed.

        Unlike query(), the output is not parsed: a script may print several results. Pass
        flags=["-bail"] to stop at the first failing statement; the error then carries the last
        lines printed before it, so .print markers show where the script was.
        """
        script = (self.preamble.rstrip() + "\n" if self.preamble else "") + sql
        completed = subprocess.run(
            [self.binary, *self.flags, "-batch", ":memory:"],
            input=script,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        if completed.returncode != 0 or completed.stderr.strip():
            tail = "\n".join(completed.stdout.splitlines()[-40:])
            raise DuckDBError(
                "{} failed:\n{}\n--- last output ---\n{}".format(
                    self.binary, completed.stderr.strip(), tail
                )
            )
        return completed.stdout
