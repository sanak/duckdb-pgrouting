# SPDX-License-Identifier: GPL-2.0-or-later
# Passed as CMAKE_PROJECT_INCLUDE by the Makefile's wasm_unittest target only.
#
# DuckDB v2.0 registers statically linked extensions from a constructor in
# extension/loader/static_extension_autoregister.cpp, which it compiles into the duckdb target.
# Under Emscripten that target is a static archive, and the unittest links it rather than the
# program-level loader, so the linker drops the unreferenced object: no extension is registered
# and every `require pgrouting` test is skipped. DuckDB's own comment on that file says to compile
# it into the program, so this compiles it into unittest once every target exists.
#
# That relies on the duckdb target being a static archive. Emscripten 3.1.71's CMake platform file
# declared shared libraries unsupported; 6.0.9's does not, so `add_library(duckdb SHARED)` builds a
# real shared module, which keeps the generated static extension loader to itself, and the
# unittest fails to link with "undefined symbol: duckdb_register_static_extensions". Declaring them
# unsupported again makes CMake build every SHARED library as STATIC, as under 3.1.71 (DuckDB's
# policy range predates CMP0164, which would make that an error); this include runs right after
# project(), before any target exists.
if(PROJECT_NAME STREQUAL "DuckDB" AND NOT DEFINED PGROUTING_WASM_UNITTEST_AUTOREGISTER)
    set(PGROUTING_WASM_UNITTEST_AUTOREGISTER TRUE)
    set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS FALSE)
    function(pgrouting_compile_autoregister_into_unittest)
        if(TARGET unittest)
            target_sources(unittest PRIVATE "${PROJECT_SOURCE_DIR}/extension/loader/static_extension_autoregister.cpp")
        endif()
    endfunction()
    cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL pgrouting_compile_autoregister_into_unittest)
endif()
