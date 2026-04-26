# ff test suite — a minimal expected-output test harness.
#
# Each .ff file under cases/ has a matching .out file. The driver runs
# ff_eval() on the .ff source, captures everything written via the
# platform vprintf callback, and compares it byte-for-byte against .out.
#
# Usage:
#   cd build && ctest
#   cd build && ctest -V     # verbose, shows driver stderr on failure

add_executable(ff_test_driver driver.c)

target_include_directories(ff_test_driver
    PRIVATE "${CMAKE_SOURCE_DIR}/src"
)

if(BUILD_SHARED_LIBS)
    target_link_libraries(ff_test_driver PRIVATE ff)
else()
    target_link_libraries(ff_test_driver PRIVATE ff_static)
endif()

target_link_libraries(ff_test_driver PRIVATE m)


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
    else()
        message(WARNING "Missing expected output: ${out}")
    endif()
endforeach()
