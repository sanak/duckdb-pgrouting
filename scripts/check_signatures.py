# SPDX-License-Identifier: GPL-2.0-or-later
"""Check that every upstream pgRouting signature of an implemented function is registered.

Upstream declares its SQL API in third_party/pgrouting/sql/sigs/pgrouting--<major>.<minor>.sig.
DuckDB reports what this extension actually registered in duckdb_functions(). This script compares
the two and fails when either side has something the other does not.
"""

import argparse
import json
import os
import re
import sys

import duckdbcli

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIG_DIR = os.path.join(REPO_ROOT, "third_party", "pgrouting", "sql", "sigs")
NOT_PORTED_PATH = os.path.join(REPO_ROOT, "test", "pgrouting_not_ported.json")

TYPE_MAP = {
    "text": "VARCHAR",
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


def candidate_keys(parameters, parameter_types):
    """Return the upstream-shaped argument lists this registered variant could be covering.

    A parameter is positional exactly when its name matches ^col\\d+$; parameter_types lists the
    positional types first and the named ones after. Key A is the named form, where `directed` is
    a trailing named parameter; key B is the positional form, where it is another colN. A variant
    with no named parameter collapses to a single key.
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
            named.append((name, cleaned))
    key_b = tuple(positional)
    key_a = key_b + tuple(t for _, t in sorted(named))
    return {key_a, key_b}


def sig_file_path(version):
    """Derive the .sig file for a pgRouting version string such as '4.0.2'."""
    parts = version.split(".")
    if len(parts) < 2:
        raise ValueError("cannot derive a sig file name from version %r" % version)
    return os.path.join(SIG_DIR, "pgrouting--%s.%s.sig" % (parts[0], parts[1]))
