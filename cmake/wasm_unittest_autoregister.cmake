# SPDX-License-Identifier: GPL-2.0-or-later
# Passed as CMAKE_PROJECT_INCLUDE by the Makefile's wasm_unittest target only.
#
# DuckDB v2.0 registers statically linked extensions from a constructor in
# extension/loader/static_extension_autoregister.cpp, which it compiles into the duckdb target.
# Under Emscripten that target is a static archive, and the unittest links it rather than the
# program-level loader, so the linker drops the unreferenced object: no extension is registered
# and every `require pgrouting` test is skipped. DuckDB's own comment on that file says to compile
# it into the program, so this compiles it into unittest once every target exists.
if(PROJECT_NAME STREQUAL "DuckDB" AND NOT DEFINED PGROUTING_WASM_UNITTEST_AUTOREGISTER)
    set(PGROUTING_WASM_UNITTEST_AUTOREGISTER TRUE)
    function(pgrouting_compile_autoregister_into_unittest)
        if(TARGET unittest)
            target_sources(unittest PRIVATE "${PROJECT_SOURCE_DIR}/extension/loader/static_extension_autoregister.cpp")
        endif()
    endfunction()
    cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL pgrouting_compile_autoregister_into_unittest)
endif()
