# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.5)

set(TARGET_NAME pgrouting)
set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})

set(CMAKE_CXX_STANDARD "17" CACHE STRING "C++ standard to enforce")
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/pgrouting_sources.cmake)

# pgRouting only uses header-only Boost libraries (Graph, Geometry).
find_package(Boost CONFIG REQUIRED)

include_directories(src/include)
# Must precede pgRouting's include/: these headers shadow the PostgreSQL-dependent upstream ones.
include_directories(src/pg_compat/include)
include_directories(${PGROUTING_DIR}/include)

set(DUCKDB_PGROUTING_SOURCES
    src/pgrouting_extension.cpp
    src/exec/exec_common.cpp
    src/exec/exec_function.cpp
    src/exec/input_registry.cpp
    src/exec/input_slots.cpp
    src/exec/request_params.cpp
    src/exec/result_emitters.cpp
    src/exec/withpoints_keys.cpp
    src/functions/allpairs_specs.cpp
    src/functions/astar_specs.cpp
    src/functions/bd_astar_specs.cpp
    src/functions/bd_dijkstra_specs.cpp
    src/functions/bellman_ford_specs.cpp
    src/functions/breadth_first_search_specs.cpp
    src/functions/chinese_specs.cpp
    src/functions/circuits_specs.cpp
    src/functions/coloring_specs.cpp
    src/functions/components_specs.cpp
    src/functions/contraction_specs.cpp
    src/functions/dag_shortest_path_specs.cpp
    src/functions/degree.cpp
    src/functions/dijkstra_specs.cpp
    src/functions/dominator_specs.cpp
    src/functions/driving_distance_specs.cpp
    src/functions/extract_vertices.cpp
    src/functions/find_close_edges.cpp
    src/functions/function_docs.cpp
    src/functions/ksp_specs.cpp
    src/functions/line_graph_specs.cpp
    src/functions/max_flow_specs.cpp
    src/functions/meta_functions.cpp
    src/functions/metrics_specs.cpp
    src/functions/mincut_specs.cpp
    src/functions/ordering_specs.cpp
    src/functions/planar_specs.cpp
    src/functions/spanning_tree_specs.cpp
    src/functions/spec_functions.cpp
    src/functions/sql_template.cpp
    src/functions/traversal_specs.cpp
    src/functions/transitive_closure_specs.cpp
    src/functions/trsp_specs.cpp
    src/functions/tsp_specs.cpp
    src/functions/withpoints_specs.cpp
    src/pg_compat/src/drivers_analysis.cpp
    src/pg_compat/src/drivers_contraction.cpp
    src/pg_compat/src/drivers_flow.cpp
    src/pg_compat/src/drivers_graph.cpp
    src/pg_compat/src/drivers_path.cpp
    src/pg_compat/src/drivers_routes.cpp
    src/pg_compat/src/drivers_tree.cpp
    src/pg_compat/src/drivers_trsp.cpp
    src/pg_compat/src/drivers_tsp.cpp
    src/pg_compat/src/drivers_unified.cpp
    src/pg_compat/src/family_drivers.cpp
    src/pg_compat/src/get_check_data.cpp
    src/pg_compat/src/pg_types.cpp)

set(EXTENSION_SOURCES ${DUCKDB_PGROUTING_SOURCES} ${PGROUTING_SOURCES})

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

# Upstream code is compiled unmodified; its warnings are not ours to fix here.
set_source_files_properties(${PGROUTING_SOURCES} PROPERTIES COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>")

foreach(target ${EXTENSION_NAME} ${LOADABLE_EXTENSION_NAME})
  # A target property, not the CMAKE_CXX_STANDARD cache entry above: DuckDB's own CMakeLists.txt is
  # the top-level project and sets that entry first, so a DuckDB release that defaults to C++11
  # would win, and pgRouting's Boost.Geometry and Boost.Math headers need C++14 at least.
  set_target_properties(${target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  target_link_libraries(${target} Boost::headers)
  # Same definitions upstream's build uses. PRIVATE, not PUBLIC: PROJECT_VERSION is a very generic
  # macro name and PUBLIC would leak it to every target that links the extension.
  target_compile_definitions(${target} PRIVATE BOOST_ALLOW_DEPRECATED_HEADERS
                                               PROJECT_VERSION="${PGROUTING_VERSION}")
  # base_graph.hpp's get_V builds an error string from __PRETTY_FUNCTION__, a GCC/Clang extension
  # MSVC does not provide. Upstream guards that identifier for MSVC everywhere else it appears
  # (interruption.hpp maps it to __FUNCSIG__, messages.hpp compiles its uses away), but uses it raw
  # there; upstream builds for Windows with MinGW, where it exists, so that line is never exercised.
  # Supplying upstream's own MSVC spelling from the build keeps the source unmodified.
  if(MSVC)
    target_compile_definitions(${target} PRIVATE __PRETTY_FUNCTION__=__FUNCSIG__)
  endif()
  # pgRouting relies on catching C++ exceptions (e.g. its Dijkstra stops the search by throwing).
  # Emscripten disables exception catching unless -fexceptions or -fwasm-exceptions is given,
  # and the wasm_mvp build passes neither.
  if(EMSCRIPTEN AND NOT CMAKE_CXX_FLAGS MATCHES "-fwasm-exceptions")
    target_compile_options(${target} PRIVATE -fexceptions)
    target_link_options(${target} PRIVATE -fexceptions)
  endif()
endforeach()

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
