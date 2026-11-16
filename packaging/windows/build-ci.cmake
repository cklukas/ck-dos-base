# Copyright (c) 2026 C. Klukas
# SPDX-License-Identifier: MIT
# Native Windows CI entry point for ck-git-hosting Windows runner (enforces V: drive policy).
cmake_minimum_required(VERSION 3.24)

if(NOT CMAKE_HOST_WIN32)
    message(FATAL_ERROR "Windows CI script must run on Windows")
endif()

if("$ENV{TMPDIR}" STREQUAL "" OR NOT IS_ABSOLUTE "$ENV{TMPDIR}")
    message(FATAL_ERROR "Select an absolute job TMPDIR on V: before building")
endif()

file(TO_CMAKE_PATH "$ENV{TMPDIR}" scratch)
if(NOT scratch MATCHES "^[Vv]:/")
    message(FATAL_ERROR "Mandatory Windows workstation policy: TMPDIR (${scratch}) must reside on drive V:")
endif()
if(NOT IS_DIRECTORY "V:/")
    message(FATAL_ERROR "Mandatory Windows development drive V: is unavailable")
endif()

set(ENV{TEMP} "${scratch}")
set(ENV{TMP} "${scratch}")
set(ENV{TMPDIR} "${scratch}")

get_filename_component(source "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(build "${scratch}/b")
set(output "${source}/dist")

file(READ "${source}/VERSION" version)
string(STRIP "${version}" version)
if("$ENV{CKGIT_REF}" MATCHES "^refs/tags/(.*)$")
    if(NOT CMAKE_MATCH_1 STREQUAL "v${version}")
        message(FATAL_ERROR "Release tag ${CMAKE_MATCH_1} must match VERSION ${version}")
    endif()
endif()

function(run_checked)
    execute_process(COMMAND ${ARGV} WORKING_DIRECTORY "${source}" RESULT_VARIABLE status)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "Windows CI command failed (${status}): ${ARGV}")
    endif()
endfunction()

file(REMOVE_RECURSE "${build}" "${output}")
file(MAKE_DIRECTORY "${output}")

get_filename_component(cmake_dir "${CMAKE_COMMAND}" DIRECTORY)
find_program(ctest_exe NAMES ctest.exe HINTS "${cmake_dir}" REQUIRED)

run_checked("${CMAKE_COMMAND}" -G Ninja
    -S "${source}"
    -B "${build}"
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_TRY_COMPILE_CONFIGURATION=Release
    -DCMAKE_C_COMPILER=cl.exe
    -DCMAKE_CXX_COMPILER=cl.exe)

run_checked("${CMAKE_COMMAND}" --build "${build}" --config Release --parallel 4)
run_checked("${ctest_exe}" --test-dir "${build}" --output-on-failure)

run_checked("${build}/ckdist.exe" release
    --repo-root "${source}"
    --tool-dir "${build}"
    --verify-manifest "${source}/docs/build-manifest.json"
    --output "${output}"
    --version "${version}"
    --platform windows-x64
    --sdk-only)

if(NOT EXISTS "${output}/ck-dos-sdk-${version}-windows-x64.zip")
    message(FATAL_ERROR "Expected Windows SDK archive missing in ${output}")
endif()
