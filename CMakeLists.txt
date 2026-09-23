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
    src/exec/input_registry.cpp
    src/exec/shortest_path_exec.cpp
    src/exec/withpoints_keys.cpp
    src/functions/function_docs.cpp
    src/functions/meta_functions.cpp
    src/functions/shortest_path_functions.cpp
    src/functions/shortest_path_specs.cpp
    src/pg_compat/src/get_check_data.cpp
    src/pg_compat/src/old_style_drivers.cpp
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
