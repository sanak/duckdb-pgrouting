# SPDX-License-Identifier: GPL-2.0-or-later
# The extension version a release build reports (duckdb_extensions().extension_version).
#
# DuckDB derives it in duckdb_extension_generate_version from
# `git describe --tags --always --match <pattern>`: the tag when that is exactly vX.Y.Z[-suffix],
# the short commit hash otherwise. DuckDB v1.5.x writes the pattern in single quotes, which CMake
# passes to git literally, so no tag ever matches and a tagged v1.5 build reports its hash; DuckDB
# fixed the quoting after v1.5.5. This function applies the same rule with the pattern unquoted
# and returns the tag, or an empty string, which leaves the version to DuckDB (the commit hash).
function(pgrouting_extension_version OUTPUT_VAR WORKING_DIR)
    set(version "")
    if(NOT GIT_EXECUTABLE)
        find_program(GIT_EXECUTABLE git)
    endif()
    if(GIT_EXECUTABLE)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --tags --always --match "v*.*.*"
            WORKING_DIRECTORY "${WORKING_DIR}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE describe
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(result EQUAL 0 AND describe MATCHES "^v[0-9]+\\.[0-9]+\\.[0-9]+(-[a-zA-Z0-9\\.]+)?$")
            set(version "${describe}")
        endif()
    endif()
    set(${OUTPUT_VAR} "${version}" PARENT_SCOPE)
endfunction()
