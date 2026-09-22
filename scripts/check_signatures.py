# SPDX-License-Identifier: GPL-2.0-or-later
"""Check that every upstream pgRouting signature of an implemented function is registered.

Upstream declares its SQL API in third_party/pgrouting/sql/sigs/pgrouting--<major>.<minor>.sig.
DuckDB reports what this extension actually registered in duckdb_functions(). This script compares
the two and fails when either side has something the other does not. A registered variant covers
an upstream signature if its positional types match the signature's prefix, and its named types
form a multiset equal to the signature's remaining types plus a suffix of the positional types.
"""

import argparse
import json
import os
import re
import sys
from collections import Counter

import duckdbcli

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIG_DIR = os.path.join(REPO_ROOT, "third_party", "pgrouting", "sql", "sigs")
NOT_PORTED_PATH = os.path.join(REPO_ROOT, "test", "pgrouting_not_ported.json")

TYPE_MAP = {
    "text": "VARCHAR",
    "character": "VARCHAR",
    "bigint": "BIGINT",
    "boolean": "BOOLEAN",
    "integer": "INTEGER",
    "double precision": "DOUBLE",
    "anyarray": "BIGINT[]",
}

SIG_LINE_RE = re.compile(r"^([a-z0-9_]+)\((.*)\)$")
POSITIONAL_RE = re.compile(r"^col\d+$")


def parse_sig_file(text):
    """Return {function name: [argument type tuple, ...]} for the public signatures only."""
    signatures = {}
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("_pgr_"):
            continue
        match = SIG_LINE_RE.match(line)
        if match is None:
            raise ValueError("unparsable signature line: %r" % raw)
        body = match.group(2).strip()
        args = tuple(a.strip() for a in body.split(",")) if body else ()
        signatures.setdefault(match.group(1), []).append(args)
    return signatures


def map_upstream_types(args):
    """Translate an upstream argument list into the DuckDB type names the catalog reports."""
    mapped = []
    for arg in args:
        if arg not in TYPE_MAP:
            raise ValueError("no DuckDB type mapped for upstream type %r" % arg)
        mapped.append(TYPE_MAP[arg])
    return tuple(mapped)


def split_variant(parameters, parameter_types):
    """Return (positional types, named types) of one registered variant.

    A parameter is positional exactly when its name matches ^col\\d+$. Types are cleaned of the
    quoting the CLI puts around list types ('BIGINT[]').
    """
    if len(parameters) != len(parameter_types):
        raise ValueError("parameters and parameter_types differ in length")
    positional = []
    named = []
    for name, type_name in zip(parameters, parameter_types):
        cleaned = type_name.strip().strip("'")
        if POSITIONAL_RE.match(name):
            positional.append(cleaned)
        else:
            named.append(cleaned)
    return tuple(positional), tuple(named)


def covers(parameters, parameter_types, signature):
    """Whether this registered variant implements upstream `signature` (DuckDB type names).

    Every variant accepts all of an overload's defaulted parameters by name, and passes the
    first m of them positionally as well, because DuckDB never matches a named parameter
    positionally. duckdb_functions() does not report named parameters in declaration order, so
    the named part is compared as a multiset: the last m positional types plus the signature's
    remaining types must be exactly the named parameters' types.
    """
    positional, named = split_variant(parameters, parameter_types)
    signature = tuple(signature)
    if signature[:len(positional)] != positional:
        return False
    rest = signature[len(positional):]
    m = len(named) - len(rest)
    if m < 0 or m > len(positional):
        return False
    return Counter(positional[len(positional) - m:] + rest) == Counter(named)


def sig_file_path(version):
    """Derive the .sig file for a pgRouting version string such as '4.0.2'."""
    parts = version.split(".")
    if len(parts) < 2:
        raise ValueError("cannot derive a sig file name from version %r" % version)
    return os.path.join(SIG_DIR, "pgrouting--%s.%s.sig" % (parts[0], parts[1]))


def load_not_ported(path):
    """Read the human-owned list of upstream functions deliberately left unimplemented."""
    if not os.path.exists(path):
        return {}
    with open(path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError("%s must hold a JSON object" % path)
    for name, entry in data.items():
        if not isinstance(entry, dict) or not entry.get("reason"):
            raise ValueError("%s: entry %r needs a non-empty \"reason\"" % (path, name))
    return data


# DuckDB's `.mode json` renders a LIST column with its own list-literal syntax (unquoted,
# comma-space separated, e.g. `[col0, col1]`) rather than a JSON array, so a query that selects
# `parameters` or `parameter_types` directly makes duckdbcli.DuckDB.query() fail with "not JSON"
# (it runs `json.loads` on the CLI's raw output). Measured directly: `SELECT parameters,
# parameter_types FROM duckdb_functions() WHERE function_name='pgr_dijkstra'` raises
# `duckdbcli.DuckDBError: not JSON`. Joining each list into one string with a separator that
# never appears in a type name or a parameter name keeps the columns scalar VARCHAR, which
# `.mode json` renders as an ordinary JSON string; splitting it back apart here recovers the list.
_LIST_SEP = "\x1f"


def collect_variants(db):
    """Return {upstream function name: [(parameters, parameter_types), ...]} from the catalog.

    tags['ext'] = 'pgrouting' also matches _pgr_shortestpath_exec, the internal in-out table
    function, which carries no pgrouting_name; the NULL filter drops it.

    The key is the tag lowercased. PostgreSQL case-folds unquoted identifiers, so a
    `CREATE FUNCTION pgr_dijkstraCost(...)` in upstream's SQL is actually named `pgr_dijkstracost`
    there, and that is the spelling `sql/sigs/pgrouting--<ver>.sig` (and
    test/pgrouting_not_ported.json) record. This extension's own `pgrouting_name` tag keeps
    upstream's CREATE FUNCTION spelling verbatim (camelCase and all), so it has to be folded here
    to line up with the already-folded signature file.
    """
    result = db.query(
        "SELECT tags['pgrouting_name'], "
        "array_to_string(parameters, chr(31)), "
        "array_to_string(parameter_types, chr(31)) "
        "FROM duckdb_functions() "
        "WHERE tags['ext'] = 'pgrouting' AND tags['pgrouting_name'] IS NOT NULL "
        "ORDER BY 1, 2, 3;"
    )
    variants = {}
    for name, parameters, parameter_types in result.rows:
        params = tuple(parameters.split(_LIST_SEP)) if parameters else ()
        types = tuple(parameter_types.split(_LIST_SEP)) if parameter_types else ()
        variants.setdefault(name.lower(), []).append((params, types))
    return variants


def main(argv=None):
    parser = argparse.ArgumentParser(description="Check pgRouting signature coverage.")
    parser.add_argument("--duckdb", default=None,
                        help="DuckDB binary with the pgrouting extension linked in")
    parser.add_argument("--quiet", action="store_true",
                        help="print failures only, not the coverage report")
    args = parser.parse_args(argv)

    db = duckdbcli.DuckDB(args.duckdb or duckdbcli.default_binary())
    version = db.query("SELECT DuckDB_pgRouting_Version();").rows[0][0]
    path = sig_file_path(version)
    if not os.path.exists(path):
        print("FAIL: no signature file %s for pgRouting %s" % (path, version))
        return 1
    with open(path, "r", encoding="utf-8") as handle:
        upstream = parse_sig_file(handle.read())

    variants = collect_variants(db)
    not_ported = load_not_ported(NOT_PORTED_PATH)
    failures = []
    lines = ["pgRouting %s, signatures from %s" % (version, path)]

    for name in sorted(variants):
        if name not in upstream:
            failures.append("%s: registered here but absent from %s" % (name, path))
            continue
        sigs = [map_upstream_types(a) for a in upstream[name]]
        for sig, raw in zip(sigs, upstream[name]):
            hits = sum(1 for p, t in variants[name] if covers(p, t, sig))
            if hits == 0:
                failures.append("%s(%s): no registered variant covers it" % (name, ",".join(raw)))
            else:
                lines.append("  %s(%s) covered by %d variant(s)" % (name, ",".join(raw), hits))
        for params, types in variants[name]:
            if not any(covers(params, types, sig) for sig in sigs):
                failures.append("%s variant (%s) covers no upstream signature"
                                % (name, ", ".join(types)))

    for name, entry in sorted(not_ported.items()):
        if name in variants:
            failures.append("%s is listed as not ported but is implemented" % name)
        elif name not in upstream:
            failures.append("%s is listed as not ported but does not exist upstream" % name)
        else:
            lines.append("  not ported: %s (%s)" % (name, entry["reason"]))

    unimplemented = sorted(set(upstream) - set(variants) - set(not_ported))
    lines.append("implemented: %d, not ported: %d, unimplemented: %d"
                 % (len(variants), len(not_ported), len(unimplemented)))
    if not args.quiet:
        print("\n".join(lines))
        print("unimplemented: " + ", ".join(unimplemented))
    for failure in failures:
        print("FAIL: " + failure)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
