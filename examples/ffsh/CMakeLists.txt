cmake_minimum_required(VERSION 3.10.2)

# Two ways this CMakeLists is reached:
#   (a) Standalone: `cmake -S examples/ffsh -B build/ffsh ...`. We need
#       to find_package(ff) to pick up the installed library.
#   (b) Sub-directory of the parent ff build (`-DFF_BUILD_EXAMPLES=ON`).
#       The `ff::ff` alias is already in scope and find_package would
#       fail (or, worse, pick up an unrelated install).
if(NOT TARGET ff::ff)
    project(ffsh
        VERSION 1.0.0
        LANGUAGES C)

    # Pick up `ff-config.cmake` from a system install (default
    # /usr/local/lib/cmake/ff) or from a custom prefix passed as
    # `-DCMAKE_PREFIX_PATH=...`.
    find_package(ff 1.0 REQUIRED)
else()
    # In-tree build — the parent project already declared `project()`.
    # Inherit its version and language settings; just declare a local
    # version variable for ffsh_version.h.
    set(FFSH_LANGUAGES C)
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_INCLUDE_CURRENT_DIR_IN_INTERFACE ON)

# Stamp the binary with build time + git hash. The two helpers come
# in via ff's installed cmake module set when needed; here we inline
# the same one-liners since we don't want a hard dependency on the
# private ff-helpers script.
string(TIMESTAMP FFSH_BUILD_TIME "%Y-%m-%d %H:%M %Z")
execute_process(COMMAND git log --no-decorate -1 --format=%h .
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_VARIABLE FFSH_GIT_HASH
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
if(FFSH_GIT_HASH STREQUAL "")
    set(FFSH_GIT_HASH "N/A")
endif()

set(FFSH_VERSION_MAJOR 1)
set(FFSH_VERSION_MINOR 0)
set(FFSH_VERSION_PATCH 0)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/ffsh_version.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/ffsh_version.h")

add_executable(ffsh "${CMAKE_CURRENT_SOURCE_DIR}/src/main.c")

target_include_directories(ffsh
    PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(ffsh PRIVATE ff::ff)
