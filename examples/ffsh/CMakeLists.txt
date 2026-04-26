cmake_minimum_required(VERSION 3.10.2)

project(ffsh
    VERSION 1.0.0
    LANGUAGES C)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_INCLUDE_CURRENT_DIR_IN_INTERFACE ON)

# Pick up `ff-config.cmake` from a system install (default
# /usr/local/lib/cmake/ff) or from a custom prefix passed as
# `-DCMAKE_PREFIX_PATH=...`.
find_package(ff 1.0 REQUIRED)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")

# Stamp the binary with build time + git hash. The two helpers come
# in via ff's installed cmake module set when needed; here we inline
# the same one-liners since we don't want a hard dependency on the
# private ff-helpers script.
string(TIMESTAMP FFSH_BUILD_TIME "%Y-%m-%d %H:%M %Z")
execute_process(COMMAND git log --no-decorate -1 --format=%h .
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                OUTPUT_VARIABLE FFSH_GIT_HASH
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
if(FFSH_GIT_HASH STREQUAL "")
    set(FFSH_GIT_HASH "N/A")
endif()

set(FFSH_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(FFSH_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(FFSH_VERSION_PATCH ${PROJECT_VERSION_PATCH})

configure_file("${CMAKE_SOURCE_DIR}/src/ffsh_version.h.in"
               "${PROJECT_BINARY_DIR}/ffsh_version.h")

add_executable(ffsh "${CMAKE_SOURCE_DIR}/src/main.c")

target_include_directories(ffsh
    PRIVATE
        "${PROJECT_BINARY_DIR}"
        "${CMAKE_SOURCE_DIR}/src")

target_link_libraries(ffsh PRIVATE ff::ff)
