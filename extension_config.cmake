# SPDX-License-Identifier: GPL-2.0-or-later
# Included by DuckDB's build system; declares the extension built from this repository.

# A release tag's build must report the tag as its version; see cmake/extension_version.cmake for
# why this is not left to DuckDB. An empty result lets DuckDB derive the version as usual.
include(${CMAKE_CURRENT_LIST_DIR}/cmake/extension_version.cmake)
pgrouting_extension_version(PGROUTING_EXTENSION_VERSION ${CMAKE_CURRENT_LIST_DIR})

duckdb_extension_load(pgrouting
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
    EXTENSION_VERSION "${PGROUTING_EXTENSION_VERSION}"
)
# DuckDB v2.0 builds a loaded extension without linking it; the shell and unittest link only the
# extensions a config lists here.
duckdb_extension_statically_link(pgrouting)
