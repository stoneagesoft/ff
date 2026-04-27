cmake_minimum_required(VERSION 3.10.2)

project(ff
    VERSION 1.0.0
    LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_INCLUDE_CURRENT_DIR_IN_INTERFACE ON)

# Build helpers (private; never installed). Drives compile flags,
# OS detection (FF_OS_*), and the file-glob / resource-compile macros
# used below.
include("${CMAKE_SOURCE_DIR}/cmake/ff-helpers.cmake")

# Memory-safety mode. When ON, every address-consuming primitive
# (`@`, `!`, `+!`, `c@`, `c!`, `s!`, `s+`, `strlen`, `strcmp`,
# `execute`, `evaluate`, `load`) validates its pointer / xt against
# the engine's tracked regions and raises FF_ERR_BAD_PTR on a miss.
# Enable for embeddings that take untrusted Forth input. See
# doc/md/20-design.md `## Memory safety`.
option(FF_SAFE_MEM "Validate every fetch/store/execute pointer." OFF)
if(FF_SAFE_MEM)
    add_compile_definitions(FF_SAFE_MEM=1)
endif()

# Control where the static and shared libraries are built so that on
# Windows we don't need to tinker with the path to run the executable.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")

ff_build_time(FF_BUILD_TIME)
ff_git_hash(FF_GIT_HASH)

# ff_version.h is generated and treated as a public header — installed
# alongside the hand-written headers and re-found via find_package().
set(FF_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(FF_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(FF_VERSION_PATCH ${PROJECT_VERSION_PATCH})
configure_file("${CMAKE_SOURCE_DIR}/src/ff_version.h.in" "${PROJECT_BINARY_DIR}/ff_version.h")

ff_rc(src)

ff_files(ff_c_src
    "src/*.c"
)

ff_files(ff_cpp_src
    "src/*.cpp"
    "src/*.cxx"
    "src/*.cc"
)

set(ff_src ${ff_c_src} ${ff_cpp_src})

add_library(ff_obj OBJECT ${ff_src})
set_property(TARGET ff_obj PROPERTY POSITION_INDEPENDENT_CODE 1)

# Independent flags for the two library variants. WASM only ever
# produces a static archive. BUILD_SHARED_LIBS is honoured as a
# shorthand for `-DFF_BUILD_SHARED=ON` when no explicit choice is made.
if(FF_OS_WASM)
    set(_ff_default_shared OFF)
elseif(DEFINED BUILD_SHARED_LIBS)
    set(_ff_default_shared ${BUILD_SHARED_LIBS})
else()
    set(_ff_default_shared OFF)
endif()
option(FF_BUILD_STATIC "Build the static archive (libff_static.a)." ON)
option(FF_BUILD_SHARED "Build the shared library (libff.so.X)." ${_ff_default_shared})

if(NOT FF_BUILD_STATIC AND NOT FF_BUILD_SHARED)
    message(FATAL_ERROR "At least one of FF_BUILD_STATIC / FF_BUILD_SHARED must be ON.")
endif()

set(FF_INSTALL_TARGETS "")

if(FF_BUILD_STATIC)
    add_library(ff_static STATIC $<TARGET_OBJECTS:ff_obj>)
    set_property(TARGET ff_static PROPERTY POSITION_INDEPENDENT_CODE 1)
    set_property(TARGET ff_static PROPERTY OUTPUT_NAME ff_static)
    ff_target_set_version(ff_static)
    list(APPEND FF_INSTALL_TARGETS ff_static)
endif()

if(FF_BUILD_SHARED AND NOT FF_OS_WASM)
    add_library(ff_shared SHARED $<TARGET_OBJECTS:ff_obj>)
    set_property(TARGET ff_shared PROPERTY POSITION_INDEPENDENT_CODE 1)
    set_property(TARGET ff_shared PROPERTY OUTPUT_NAME ff)
    ff_target_set_version(ff_shared)
    list(APPEND FF_INSTALL_TARGETS ff_shared)
endif()

# Namespaced alias so examples / sibling subdirs can write
# `target_link_libraries(app PRIVATE ff::ff)` exactly like a downstream
# consumer that picked us up via find_package(). Prefers the shared
# variant when both are built, matching the cmake config logic in
# ff-config.cmake.in.
if(NOT TARGET ff::ff)
    if(TARGET ff_shared)
        add_library(ff::ff ALIAS ff_shared)
    elseif(TARGET ff_static)
        add_library(ff::ff ALIAS ff_static)
    endif()
endif()

set(FF_INCLUDES ${FF_INCLUDES} ${PROJECT_BINARY_DIR})

_ff(ff_obj)

if(NOT FF_OS_WASM)
    ff_precompile_headers(ff_obj)
endif()

# Public include directories and link deps the imported targets
# advertise to downstream consumers. BUILD_INTERFACE is what in-tree
# examples see; INSTALL_INTERFACE is what `find_package(ff)` exposes.
foreach(_ff_t ${FF_INSTALL_TARGETS})
    target_include_directories(${_ff_t}
        INTERFACE
            $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/ff>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/ff/3rdparty>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(${_ff_t} PUBLIC ${FF_LIB_DIRS} ${FF_LIBS})
endforeach()


################
# INSTALLATION #
################

# Default to /usr/local on UNIX (GNUInstallDirs gives us the right
# subdir layout for free). The WASM build keeps its dedicated prefix.
if(FF_OS_WASM AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/opt/ff.wasm" CACHE PATH "Installation prefix" FORCE)
endif()

install(TARGETS ${FF_INSTALL_TARGETS}
        EXPORT ff-targets
        ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}     COMPONENT libraries
        LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}     COMPONENT libraries
        RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}     COMPONENT libraries
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ff)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ff
        COMPONENT headers
        FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
        PATTERN "*.hxx"
        PATTERN "words" EXCLUDE)

install(
    FILES "${PROJECT_BINARY_DIR}/ff_version.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ff
    COMPONENT headers)

# Generated package config files. Live under
# <prefix>/lib/cmake/ff/ so multiple packages can coexist there.
set(FF_CMAKE_CONFIG_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/ff)

install(EXPORT ff-targets
        FILE ff-targets.cmake
        NAMESPACE ff::
        DESTINATION ${FF_CMAKE_CONFIG_DIR}
        COMPONENT development)

configure_package_config_file(
    "${CMAKE_SOURCE_DIR}/cmake/ff-config.cmake.in"
    "${PROJECT_BINARY_DIR}/ff-config.cmake"
    INSTALL_DESTINATION ${FF_CMAKE_CONFIG_DIR})

write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/ff-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
    "${PROJECT_BINARY_DIR}/ff-config.cmake"
    "${PROJECT_BINARY_DIR}/ff-config-version.cmake"
    DESTINATION ${FF_CMAKE_CONFIG_DIR}
    COMPONENT development)

# pkg-config support for non-CMake consumers. Generated at configure
# time; consumers reach it via `pkg-config --cflags --libs ff`.
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/ff.pc.in"
    "${PROJECT_BINARY_DIR}/ff.pc"
    @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/ff.pc"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
        COMPONENT development)

# Documentation: install whatever the doc/ Makefile has produced
# (Doxygen HTML tree and the timestamped PDF). Both are optional —
# `make install` does not depend on `make pdf`.
add_subdirectory(doc)


# Test suite. Build with -DFF_BUILD_TESTS=OFF to skip.
option(FF_BUILD_TESTS "Build ff test suite." OFF)
if(FF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

# Example programs (currently just the ffsh shell). Off by default
# because the standalone example tree under examples/ffsh/ is the
# canonical "embed ff via find_package" demonstration; the in-tree
# subdir build is a convenience for developers running `cmake -B
# build` and wanting an ffsh binary in one go.
option(FF_BUILD_EXAMPLES "Build the example programs under examples/." OFF)
if(FF_BUILD_EXAMPLES)
    add_subdirectory(examples/ffsh)
endif()

# libFuzzer harness. Off by default; enabling requires Clang plus the
# `-fsanitize=fuzzer,address,undefined` flags (see fuzz/CMakeLists.txt
# for the full invocation example).
option(FF_BUILD_FUZZ "Build the libFuzzer harness under fuzz/." OFF)
if(FF_BUILD_FUZZ)
    add_subdirectory(fuzz)
endif()
