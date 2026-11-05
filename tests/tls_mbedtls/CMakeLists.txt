# The reference TLS hook of the tests (docs/specs/S0001.md): the hook over Mbed TLS and a driver that makes one https request through the library.
# Mbed TLS is fetched here, at the configure time of this test project, and never becomes a part of the library, of the wheel or of the sdist.
#
#   cmake -S tests/tls_mbedtls -B build -DFUSEDWASI_INCLUDE_DIR=<src/fusedwasi/include/fusedwasi/p2>
#   cmake --build build
#
# Offline: -DFETCHCONTENT_SOURCE_DIR_MBEDTLS=<an unpacked mbedtls-3.6.7>
cmake_minimum_required(VERSION 3.14)
project(fusedwasi_tls_mbedtls LANGUAGES C)

if(NOT FUSEDWASI_INCLUDE_DIR)
    message(FATAL_ERROR "set FUSEDWASI_INCLUDE_DIR to src/fusedwasi/include/fusedwasi/p2")
endif()

include(FetchContent)

# The pin: the release archive of Mbed TLS 3.6.7 (Apache-2.0 OR GPL-2.0-or-later), by URL and by hash: a tag can move, a hash cannot. A new version is a new pair of lines here.
set(FUSEDWASI_MBEDTLS_URL "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2" CACHE STRING "the archive of Mbed TLS")
set(FUSEDWASI_MBEDTLS_SHA256 "a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6" CACHE STRING "its SHA-256")

set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
set(GEN_FILES OFF CACHE BOOL "" FORCE)
# the trimmed configuration, for the libraries and for the driver
add_compile_definitions(MBEDTLS_CONFIG_FILE="${CMAKE_CURRENT_SOURCE_DIR}/mbedtls_trim.h")

FetchContent_Declare(mbedtls URL "${FUSEDWASI_MBEDTLS_URL}" URL_HASH SHA256=${FUSEDWASI_MBEDTLS_SHA256})
FetchContent_MakeAvailable(mbedtls)

add_executable(tls_driver driver.c)
target_include_directories(tls_driver PRIVATE "${FUSEDWASI_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(tls_driver PRIVATE mbedtls mbedx509 mbedcrypto)
