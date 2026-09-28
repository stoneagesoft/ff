# ff test suite — a minimal expected-output test harness.
#
# Each .ff file under cases/ has a matching .out file. The driver runs
# ff_eval() on the .ff source, captures everything written via the
# platform vprintf callback, and compares it byte-for-byte against .out.
#
# Usage:
#   cd build && ctest
#   cd build && ctest -V     # verbose, shows driver stderr on failure

if(BUILD_SHARED_LIBS)
    set(_ff_test_lib ff)
else()
    set(_ff_test_lib ff_static)
endif()

add_executable(ff_test_driver driver.c)

target_include_directories(ff_test_driver
    PRIVATE "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(ff_test_driver PRIVATE ${_ff_test_lib} m)


# API-level checks: engine state after errors, error locations, ff_load,
# the watchdog across nested evaluation. It includes <ff_p.h> the way a
# native-word author does, so it is built with strict warnings and without
# the project's -Wno-unused-function: a private header that doesn't
# compile cleanly for a consumer fails the build here.

add_executable(ff_api_test api_test.c)

target_include_directories(ff_api_test
    PRIVATE "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(ff_api_test PRIVATE ${_ff_test_lib} m)

if(NOT MSVC)
    target_compile_options(ff_api_test PRIVATE
        -Wall -Wextra -Wunused-function -Werror)
endif()

add_test(NAME api COMMAND ff_api_test)
# A regression in the watchdog tests would otherwise hang, not fail.
set_tests_properties(api PROPERTIES TIMEOUT 60)


# Discover every .ff / .out pair and register it with ctest.

file(GLOB TEST_CASES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/cases"
     "${CMAKE_CURRENT_SOURCE_DIR}/cases/*.ff")

foreach(case_file ${TEST_CASES})
    string(REGEX REPLACE "\\.ff$" "" case_name "${case_file}")
    set(in  "${CMAKE_CURRENT_SOURCE_DIR}/cases/${case_name}.ff")
    set(out "${CMAKE_CURRENT_SOURCE_DIR}/cases/${case_name}.out")
    # Cases tagged "memsafe" exercise out-of-range pointers and would
    # segfault the driver under the default build. Skip them unless
    # FF_SAFE_MEM is on.
    if(case_name MATCHES "memsafe" AND NOT FF_SAFE_MEM)
        continue()
    endif()
    if(EXISTS "${out}")
        add_test(NAME ${case_name}
                 COMMAND ff_test_driver "${in}" "${out}")
        # The driver's watchdog bounds a runaway loop, but not a regression
        # that defeats the watchdog itself; fail fast rather than hang.
        set_tests_properties(${case_name} PROPERTIES TIMEOUT 30)
    else()
        message(WARNING "Missing expected output: ${out}")
    endif()
endforeach()
