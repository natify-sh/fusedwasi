cmake_minimum_required(VERSION 3.13)

# ── Version: from git-tag or fallback ────────────────────────────────
set(FUSEDWASI_VERSION_MAJOR 0)
set(FUSEDWASI_VERSION_MINOR 0)
set(FUSEDWASI_VERSION_PATCH 0)
set(FUSEDWASI_VERSION "${FUSEDWASI_VERSION_MAJOR}.${FUSEDWASI_VERSION_MINOR}.${FUSEDWASI_VERSION_PATCH}-dev")

find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(GIT_TAG MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)")
        set(FUSEDWASI_VERSION_MAJOR ${CMAKE_MATCH_1})
        set(FUSEDWASI_VERSION_MINOR ${CMAKE_MATCH_2})
        set(FUSEDWASI_VERSION_PATCH ${CMAKE_MATCH_3})
        string(REGEX REPLACE "^v" "" FUSEDWASI_VERSION "${GIT_TAG}")
    elseif(GIT_TAG)
        set(FUSEDWASI_VERSION "0.0.0-git-${GIT_TAG}")
    endif()
endif()

project(fusedwasi
    VERSION ${FUSEDWASI_VERSION_MAJOR}.${FUSEDWASI_VERSION_MINOR}.${FUSEDWASI_VERSION_PATCH}
    LANGUAGES C)

message(STATUS "fusedwasi version: ${FUSEDWASI_VERSION}")

# ── Options ──────────────────────────────────────────────────────────
option(FUSEDWASI_INSTALL "Install headers and the CMake package" ON)

# ── Generation version.h ────────────────────────────────────────────
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/fusedwasi/version.h"
    @ONLY)

# ── Header-only INTERFACE target ────────────────────────────────────
add_library(fusedwasi_headers INTERFACE)
add_library(fusedwasi::headers ALIAS fusedwasi_headers)
set_target_properties(fusedwasi_headers PROPERTIES EXPORT_NAME headers)

target_include_directories(fusedwasi_headers INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/fusedwasi/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include>)

# ── Install ────────────────────────────────────────────────────────
if(FUSEDWASI_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    # Headers
    install(DIRECTORY src/fusedwasi/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/generated/fusedwasi/version.h"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/fusedwasi)

    # Tables of the signatures of the WASI imports and the Protocol stubs (data, as the Python package carries them)
    install(DIRECTORY src/fusedwasi/signatures/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/fusedwasi/signatures
        PATTERN "__pycache__" EXCLUDE
        FILES_MATCHING PATTERN "*.json" PATTERN "*.pyi")

    # The public names, in C and in Python, with a line of purpose each (data: docs/public_api.md is made from it)
    install(FILES src/fusedwasi/public_api.json
        DESTINATION ${CMAKE_INSTALL_DATADIR}/fusedwasi)

    install(TARGETS fusedwasi_headers
        EXPORT fusedwasi-targets
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(EXPORT fusedwasi-targets
        FILE        fusedwasi-targets.cmake
        NAMESPACE   fusedwasi::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fusedwasi)

    # Config + version files
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/fusedwasi-config.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/fusedwasi-config.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fusedwasi)

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/fusedwasi-config-version.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/fusedwasi-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/fusedwasi-config-version.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fusedwasi)
endif()
