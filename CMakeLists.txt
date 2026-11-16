cmake_minimum_required(VERSION 3.20)

project(DOSBaseTools VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)

if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
    set(_dosbase_tests_default ${BUILD_TESTING})
else()
    set(_dosbase_tests_default OFF)
endif()

option(DOSBASE_BUILD_TOOLS "Build the CK-DOS Base command-line tools" ON)
option(DOSBASE_BUILD_TESTS "Build the CK-DOS Base unit tests" ${_dosbase_tests_default})
option(DOSBASE_INSTALL "Generate CK-DOS Base install and package rules" ${PROJECT_IS_TOP_LEVEL})

set(DOSBASE_MSDOS_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ms-dos-4/src"
    CACHE PATH "Path to the preserved MS-DOS 4 source tree")

function(dosbase_strict target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(dosbase_devtools
    src/devtools/core/compress_core.c
    src/devtools/core/convert_core.c
    src/devtools/core/diskimg_core.c
    src/devtools/core/exe2bin_core.c
    src/devtools/core/exefix_core.c
    src/devtools/core/lib_core.c
    src/devtools/core/link_core.c
    src/devtools/core/msgtools_core.c
    src/devtools/core/mz_core.c
    src/devtools/core/nmake_core.c
    src/devtools/core/omf_core.c
    src/devtools/asc2hlp.cpp
    src/devtools/binary.cpp
    src/devtools/cl_compiler.cpp
    src/devtools/cl_preproc.cpp
    src/devtools/compress.cpp
    src/devtools/convert.cpp
    src/devtools/dbof.cpp
    src/devtools/dos_file.cpp
    src/devtools/dump_tool.cpp
    src/devtools/exe2bin.cpp
    src/devtools/exefix.cpp
    src/devtools/lib_writer.cpp
    src/devtools/link_image.cpp
    src/devtools/link_response.cpp
    src/devtools/masm.cpp
    src/devtools/menubld.cpp
    src/devtools/msgtools.cpp
    src/devtools/mz_image.cpp
    src/devtools/nmake_exec.cpp
    src/devtools/nmake_macros.cpp
    src/devtools/nmake_plan.cpp
    src/devtools/omf_library.cpp
    src/devtools/omf_module.cpp
    src/devtools/omf_record.cpp)
add_library(dosbase::devtools ALIAS dosbase_devtools)
target_compile_features(dosbase_devtools PUBLIC cxx_std_20)
target_include_directories(dosbase_devtools PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
dosbase_strict(dosbase_devtools)

set(DOSBASE_TOOLS
    ckmzdump
    ckomfdump
    cklibdump
    ckexe2bin
    ckconvert
    ckexefix
    ckmenubld
    ckasc2hlp
    ckcompress
    cklib
    ckdbof
    ckbuildidx
    cknosrvbld
    ckbuildmsg
    ckmasm
    ckcl
    cklink
    cknmake
    ckmkimg
    ckdist)

if(DOSBASE_BUILD_TOOLS)
    foreach(tool IN LISTS DOSBASE_TOOLS)
        add_executable(${tool} src/devtools/${tool}_main.cpp)
        target_link_libraries(${tool} PRIVATE dosbase::devtools)
        dosbase_strict(${tool})
    endforeach()
endif()

if(DOSBASE_BUILD_TESTS)
    add_executable(dosbase_tests
        tests/test_main.cpp
        tests/test_asc2hlp.cpp
        tests/test_cl.cpp
        tests/test_compress.cpp
        tests/test_convert.cpp
        tests/test_dbof.cpp
        tests/test_diskimg.cpp
        tests/test_exe2bin.cpp
        tests/test_exefix.cpp
        tests/test_lib_dictionary.cpp
        tests/test_library.cpp
        tests/test_link_response.cpp
        tests/test_masm.cpp
        tests/test_menubld.cpp
        tests/test_msgtools.cpp
        tests/test_mz.cpp
        tests/test_nmake_exec.cpp
        tests/test_nmake_macros.cpp
        tests/test_nmake_plan.cpp
        tests/test_omf.cpp
        tests/test_omf_module.cpp
        tests/test_runtime.cpp)
    target_link_libraries(dosbase_tests PRIVATE dosbase::devtools)
    target_compile_definitions(dosbase_tests PRIVATE
        DOSBASE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(DOSBASE_BUILD_TOOLS)
        target_compile_definitions(dosbase_tests PRIVATE
            DOSBASE_TOOL_DIR="$<TARGET_FILE_DIR:cknmake>")
    endif()
    dosbase_strict(dosbase_tests)
    add_test(NAME dosbase_tools COMMAND dosbase_tests)
    set_tests_properties(dosbase_tools PROPERTIES TIMEOUT 120)
endif()

if(DOSBASE_INSTALL)
    include(CMakePackageConfigHelpers)
    set(_dosbase_install_targets dosbase_devtools)
    if(DOSBASE_BUILD_TOOLS)
        list(APPEND _dosbase_install_targets ${DOSBASE_TOOLS})
    endif()
    install(TARGETS ${_dosbase_install_targets}
        EXPORT DOSBaseToolsTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES LICENSE NOTICE README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
    install(EXPORT DOSBaseToolsTargets
        NAMESPACE dosbase::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DOSBaseTools)
    configure_package_config_file(
        cmake/DOSBaseToolsConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/DOSBaseToolsConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DOSBaseTools)
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/DOSBaseToolsConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/DOSBaseToolsConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/DOSBaseToolsConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DOSBaseTools)

    set(CPACK_PACKAGE_NAME "ck-dos-base-tools")
    set(CPACK_PACKAGE_VENDOR "C. Klukas")
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_PACKAGE_FILE_NAME
        "ck-dos-base-tools-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
    set(CPACK_GENERATOR "TGZ")
    include(CPack)
endif()
