# SPDX-License-Identifier: GPL-2.0-or-later
#
# Upstream pgRouting sources compiled into the extension. The list is explicit (no globbing) so
# that every upstream tag bump shows exactly which files the extension depends on.
#
# Never add a file that talks to PostgreSQL directly (SPI, palloc, elog, fmgr, SRF wrappers,
# *_process.cpp). Those layers are replaced by the extension's own code. Files that only need a
# small, replaceable set of PostgreSQL symbols are added together with their replacements.

set(PGROUTING_DIR ${CMAKE_CURRENT_LIST_DIR}/../third_party/pgrouting)

# Single source of truth for the bundled pgRouting version: upstream's own project() call.
file(STRINGS ${PGROUTING_DIR}/CMakeLists.txt _pgr_project_line REGEX "^project\\(PGROUTING VERSION")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" PGROUTING_VERSION "${_pgr_project_line}")
if(NOT PGROUTING_VERSION)
  message(FATAL_ERROR "Could not read the pgRouting version from ${PGROUTING_DIR}/CMakeLists.txt")
endif()

# Sources that need nothing from PostgreSQL.
set(PGROUTING_SOURCES
    ${PGROUTING_DIR}/src/common/assert.cpp
    ${PGROUTING_DIR}/src/common/basic_edge.cpp
    ${PGROUTING_DIR}/src/common/basic_vertex.cpp
    ${PGROUTING_DIR}/src/common/ch_edge.cpp
    ${PGROUTING_DIR}/src/common/ch_vertex.cpp
    ${PGROUTING_DIR}/src/common/identifier.cpp
    ${PGROUTING_DIR}/src/common/path.cpp
    ${PGROUTING_DIR}/src/common/xy_vertex.cpp
    ${PGROUTING_DIR}/src/components/componentsResult.cpp
    ${PGROUTING_DIR}/src/cpp_common/bpoint.cpp
    ${PGROUTING_DIR}/src/cpp_common/compPaths.cpp
    ${PGROUTING_DIR}/src/cpp_common/Dmatrix.cpp
    ${PGROUTING_DIR}/src/cpp_common/messages.cpp
    ${PGROUTING_DIR}/src/cpp_common/rule.cpp
    ${PGROUTING_DIR}/src/withPoints/withPoints.cpp)

# Sources that reach PostgreSQL only through symbols this extension replaces
# (src/pg_compat provides the stub headers and the replaced definitions).
list(APPEND PGROUTING_SOURCES
    ${PGROUTING_DIR}/src/bdDijkstra/bdDijkstra_driver.cpp
    ${PGROUTING_DIR}/src/bellman_ford/bellman_ford_driver.cpp
    ${PGROUTING_DIR}/src/bellman_ford/edwardMoore_driver.cpp
    ${PGROUTING_DIR}/src/breadthFirstSearch/binaryBreadthFirstSearch_driver.cpp
    ${PGROUTING_DIR}/src/components/components.cpp
    ${PGROUTING_DIR}/src/components/connectedComponents_driver.cpp
    ${PGROUTING_DIR}/src/cpp_common/combinations.cpp
    ${PGROUTING_DIR}/src/cpp_common/pgdata_fetchers.cpp
    ${PGROUTING_DIR}/src/cpp_common/pgdata_getters.cpp
    ${PGROUTING_DIR}/src/dagShortestPath/dagShortestPath_driver.cpp
    ${PGROUTING_DIR}/src/dijkstra/shortestPath_driver.cpp)
