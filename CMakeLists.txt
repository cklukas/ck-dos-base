cmake_minimum_required(VERSION 3.20)

project(DOSBaseTools VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)
include(CMakePackageConfigHelpers)

function(dosbase_strict target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(dosbase_devtools
    src/devtools/binary.cpp
    src/devtools/dbof.cpp
    src/devtools/dos_file.cpp
    src/devtools/dump_tool.cpp
    src/devtools/exe2bin.cpp
    src/devtools/lib_writer.cpp
    src/devtools/link_image.cpp
    src/devtools/link_response.cpp
    src/devtools/masm.cpp
    src/devtools/msgtools.cpp
    src/devtools/mz_image.cpp
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
    cklib
    ckdbof
    ckbuildidx
    cknosrvbld
    ckbuildmsg
    ckmasm
    cklink
    cknmake)

foreach(tool IN LISTS DOSBASE_TOOLS)
    add_executable(${tool} src/devtools/${tool}_main.cpp)
    target_link_libraries(${tool} PRIVATE dosbase::devtools)
    dosbase_strict(${tool})
endforeach()

if(BUILD_TESTING)
    add_executable(dosbase_tests
        tests/test_main.cpp
        tests/test_dbof.cpp
        tests/test_exe2bin.cpp
        tests/test_lib_dictionary.cpp
        tests/test_library.cpp
        tests/test_link_response.cpp
        tests/test_masm.cpp
        tests/test_msgtools.cpp
        tests/test_mz.cpp
        tests/test_nmake_macros.cpp
        tests/test_nmake_plan.cpp
        tests/test_omf.cpp
        tests/test_omf_module.cpp)
    target_link_libraries(dosbase_tests PRIVATE dosbase::devtools)
    dosbase_strict(dosbase_tests)
    add_test(NAME dosbase_tools COMMAND dosbase_tests)
    set_tests_properties(dosbase_tools PROPERTIES TIMEOUT 120)
endif()

install(TARGETS dosbase_devtools ${DOSBASE_TOOLS}
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
