# Host-side gtest for the wodle CrossPoint port — covers the port shims
# (hand-written blind: MD5, base64, String) plus upstream suites re-pointed at
# the vendored sources.
#
#   cmake -S firmware/crosspoint/test -B /tmp/cp_test
#   cmake --build /tmp/cp_test -j8
#   ctest --test-dir /tmp/cp_test --output-on-failure

cmake_minimum_required(VERSION 3.16)
project(crosspoint_wodle_tests C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(PORT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_library(cp_test_common INTERFACE)
target_include_directories(cp_test_common INTERFACE
  ${PORT_ROOT}/vendor
  ${PORT_ROOT}/vendor/lib
)
target_compile_options(cp_test_common INTERFACE -Wall -Wextra)

enable_testing()
include(GoogleTest)

add_subdirectory(port_shims)
add_subdirectory(streaming_json_parser)
add_subdirectory(release_json_parser)
add_subdirectory(zipfile)
add_subdirectory(sdcardfont)
add_subdirectory(i18n)
add_subdirectory(textencoding)
add_subdirectory(txtchapters)
add_subdirectory(epub)
add_subdirectory(render)
add_subdirectory(hyphenation)
