# Build-time helpers for the ff source tree.
#
# This file is intentionally NOT installed: it carries macros only the
# in-tree build needs (file globbing, resource compilation, version
# stamping). Downstream consumers go through find_package(ff) and the
# generated ff-config.cmake instead.

###########################################################################
# Variables this file is expected to populate / consume:
#
#   FF_OS_WASM, FF_OS_WINDOWS, FF_OS_UNIX, FF_OS_LINUX, FF_OS_MAC  (out)
#   FF_DEBUG                                                       (out)
#   FF_C_FLAGS, FF_CXX_FLAGS                                       (in/out)
#   FF_DEFINES, FF_LIBS, FF_LIB_DIRS                               (in/out)
#   FF_INCLUDES                                                    (out)
#
# FF_VERSION* are set by project(... VERSION ...) in the root CMakeLists
# and surfaced here only for use by the helpers below.
###########################################################################

string(COMPARE EQUAL "${CMAKE_BUILD_TYPE}" "Debug" FF_DEBUG)

if(FF_WASM)
    set(CMAKE_C_COMPILER "${EM_ROOT}/emcc")
    set(CMAKE_CXX_COMPILER "${EM_ROOT}/em++")
endif()

# Specify the C standard.
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED True)

# Specify the C++ standard.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_FIND_LIBRARY_PREFIXES lib)

if(FF_WASM)
    set(FF_OS_WASM TRUE)
    add_compile_definitions(FF_OS_WASM)
elseif(WIN32 OR MSYS OR MINGW)
    set(FF_OS_WINDOWS TRUE)
    add_compile_definitions(FF_OS_WIN FF_OS_WINDOWS)
elseif(UNIX)
    set(FF_OS_UNIX TRUE)
    add_compile_definitions(FF_OS_UNIX)
    if(APPLE)
        set(FF_OS_MAC TRUE)
        add_compile_definitions(FF_OS_MAC)
    else()
        set(FF_OS_LINUX TRUE)
        add_compile_definitions(FF_OS_LINUX)
    endif()
endif()

macro(ff_header_dirs root return_list)
    file(
        GLOB_RECURSE file_list
        LIST_DIRECTORIES true
        "${root}/*"
    )
    foreach (file_path ${file_list})
        get_filename_component(dir_path ${file_path} DIRECTORY)
        if(NOT EXISTS "${dir_path}/IGNORE")
            set(dir_list ${dir_list} ${dir_path})
        endif()
    endforeach()
    list(REMOVE_DUPLICATES dir_list)
    set(${return_list} ${dir_list})
endmacro()

macro(ff_files output_var)
    set(files_list "")

    foreach(wc ${ARGN})
        file(GLOB_RECURSE files "${wc}")
        foreach (file_path ${files})
            get_filename_component(dir_path ${file_path} DIRECTORY)
            if(NOT EXISTS "${dir_path}/IGNORE")
                set(files_list ${files_list} ${file_path})
            endif()
        endforeach()
    endforeach()

    set(${output_var} ${files_list})
endmacro()

macro(ff_rc root)
    file(
        GLOB_RECURSE list
        "${root}/*.rc"
    )

    set(FF_RC "tools/ffrc.py")

    foreach (file_path ${list})
        execute_process(COMMAND ${FF_RC} ${file_path} "${PROJECT_BINARY_DIR}"
                        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                        RESULT_VARIABLE FF_EXIT_CODE)
        if(FF_EXIT_CODE)
            message(FATAL_ERROR "Failed to process ${file_path} with Resource Compiler.")
        endif()
    endforeach()
endmacro()

macro(ff_build_time output_time)
    string(TIMESTAMP ${output_time} "%Y-%m-%d %H:%M %Z")
endmacro()

macro(ff_git_hash output_hash)
    execute_process(COMMAND git log --no-decorate -1 --remove-empty --no-merges --format=%h .
                    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                    OUTPUT_VARIABLE ${output_hash}
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE FF_EXIT_CODE)
    if(FF_EXIT_CODE)
        message(WARNING "Failed to get Git hash.")
        set(${output_hash} "N/A")
    endif()
endmacro()

# Print all variables.
# ARGV0 --- regex to filter.
# See https://cmake.org/cmake/help/latest/command/string.html#regex-specification
function(ff_dump_cmake_variables)
    message(CHECK_START "Show variables")
    get_cmake_property(_variable_names VARIABLES)
    list (SORT _variable_names)
    foreach (_variable_name ${_variable_names})
        if (NOT DEFINED ARGV0 OR _variable_name MATCHES "${ARGV0}")
            message(STATUS "  ${_variable_name}=${${_variable_name}}")
        endif()
    endforeach()
    message(CHECK_PASS "End")
endfunction()

ff_header_dirs("${CMAKE_SOURCE_DIR}/src" FF_INCLUDES)
set(FF_INCLUDES ${FF_INCLUDES} ${CMAKE_SOURCE_DIR}/src/ff/3rdparty)

###########
# General #
###########

if(FF_OS_WASM)
    add_compile_options(-sMEMORY64=0
                        -sDISABLE_EXCEPTION_CATCHING=1)

    add_link_options(
        -sMEMORY64=0
        -sWASM=1
        -sALLOW_MEMORY_GROWTH=1
        -sNO_EXIT_RUNTIME=0
        -sASSERTIONS=1
        -sDISABLE_EXCEPTION_CATCHING=1
        -sSINGLE_FILE=1
        -sSINGLE_FILE_BINARY_ENCODE=1
    )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_compile_definitions(
        _GNU_SOURCE
        _FILE_OFFSET_BITS=64
        _LARGEFILE64_SOURCE
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_compile_definitions(_USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
elseif(CMAKE_GENERATOR MATCHES "Visual Studio"
        AND (CMAKE_C_COMPILER_ID MATCHES "MSVC|Intel"
                OR CMAKE_CXX_COMPILER_ID MATCHES "MSVC|Intel"))
    add_compile_options(/MP)
    add_compile_options(/bigobj)
endif()

if(FF_DEBUG)
    add_compile_definitions(FF_DEBUG)
    message(STATUS "Debug mode")

    if(NOT FF_OS_WASM)
        # Use ccache for debug builds.
        find_program(CCACHE_FOUND ccache)
        if(CCACHE_FOUND)
            set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
            set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
        endif()
    endif()
else()
    message(STATUS "Release mode")
endif()

# Compiler-specific warning, exception, and optimisation flags.
# MSVC has its own flag spelling and does not accept GCC/Clang's -W
# / -O / -g / -fno-exceptions forms; gate the GCC-style block.
if(MSVC)
    set(FF_C_FLAGS   "${FF_C_FLAGS} /W3 /WX")
    set(FF_CXX_FLAGS "${FF_CXX_FLAGS} /W3 /WX /EHsc")
    # MSVC emits codegen / debug flags from CMAKE_BUILD_TYPE; nothing
    # to add here for Release vs Debug.
else()
    set(FF_C_FLAGS
        "${FF_C_FLAGS} \
        -Wall -Wextra -Werror \
        -Wno-unused-result \
        -Wno-misleading-indentation \
        -Wno-unused-parameter \
        -Wno-unused-function")

    set(FF_CXX_FLAGS
        "${FF_CXX_FLAGS} \
        -Wall -Wextra -Werror \
        -Wno-unused-result \
        -Wno-misleading-indentation")

    # GCC-only false positives we have to tolerate:
    # - -Wmaybe-uninitialized fires inside vendored md4c.c (the
    #   warning is a known GCC limitation, not a real bug).
    # - -Warray-bounds trips through the macro stack inside ff_exec
    #   (e.g. _FF_SL expanding through stack-cell access at high
    #   compile-time depth) on GCC 13+. Clang gets it right.
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        set(FF_C_FLAGS "${FF_C_FLAGS} \
            -Wno-error=maybe-uninitialized \
            -Wno-error=array-bounds")
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(FF_CXX_FLAGS "${FF_CXX_FLAGS} \
            -Wno-error=maybe-uninitialized \
            -Wno-error=array-bounds")
    endif()

    if(NOT FF_OS_WASM)
        add_compile_options(-fno-exceptions)
    endif()

    if(FF_DEBUG)
        set(FF_C_FLAGS "${FF_C_FLAGS} -O0 -ggdb")
        set(FF_CXX_FLAGS "${FF_C_FLAGS} -O0 -ggdb")
    elseif(FF_OS_WASM)
        add_compile_options(-O3 -g0)
        add_link_options(-O1 --no-optimize)
    else()
        set(FF_C_FLAGS "${FF_C_FLAGS} -O3 -g0")
        set(FF_CXX_FLAGS "${FF_C_FLAGS} -O3 -g0")
    endif()
endif()

if(NOT FF_OS_WASM)
    set(FF_LIBS ${FF_LIBS} pthread m)
    set(FF_DEFINES ${FF_DEFINES}
        -D_GNU_SOURCE
        -D_FILE_OFFSET_BITS=64
        -D_LARGEFILE64_SOURCE)
endif()

find_package(PkgConfig REQUIRED)


########
# FORT #
########

# Actually, we need this on Windows only.
set(FF_DEFINES ${FF_DEFINES}
    -DFT_CONGIG_DISABLE_WCHAR)


########
# MD4C #
########

set(FF_DEFINES ${FF_DEFINES}
    -DMD4C_USE_UTF8)


macro(ff_target_set_version target)
    set_property(TARGET ${target} PROPERTY VERSION ${PROJECT_VERSION})
    set_property(TARGET ${target} PROPERTY SOVERSION ${PROJECT_VERSION_MAJOR})
    set_property(TARGET ${target} PROPERTY INTERFACE_${target}_MAJOR_VERSION ${PROJECT_VERSION_MAJOR})
    set_property(TARGET ${target} APPEND PROPERTY COMPATIBLE_INTERFACE_STRING ${target}_MAJOR_VERSION)
endmacro()

# Apply the project-wide compile settings to a target.
macro(_ff target)
    include_directories(${FF_INCLUDES})
    add_compile_definitions(${FF_DEFINES})
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FF_C_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${FF_CXX_FLAGS}")
endmacro()

macro(ff_precompile_headers target)
    target_precompile_headers(${target} PUBLIC
        "$<BUILD_INTERFACE:$<$<COMPILE_LANGUAGE:CXX>:<algorithm$<ANGLE-R>>>"
        "$<BUILD_INTERFACE:$<$<COMPILE_LANGUAGE:CXX>:<chrono$<ANGLE-R>>>"
        "$<BUILD_INTERFACE:$<$<COMPILE_LANGUAGE:CXX>:<expected$<ANGLE-R>>>"
        "$<BUILD_INTERFACE:$<$<COMPILE_LANGUAGE:CXX>:<filesystem$<ANGLE-R>>>"
        "$<BUILD_INTERFACE:$<$<COMPILE_LANGUAGE:CXX>:<format$<ANGLE-R>>>"
        "$<BUILD_INTERFACE:$<$<COMPILE_LANGUAGE:CXX>:<functional$<ANGLE-R>>>"
        "$<BUILD_INTERFACE:$<$<COMPILE_LANGUAGE:CXX>:<limits$<ANGLE-R>>>"
        "$<BUILD_INTERFACE:$<$<COMPILE_LANGUAGE:CXX>:<list$<ANGLE-R>>>"
        "$<BUILD_INTERFACE:$<$<COMPILE_LANGUAGE:CXX>:<map$<ANGLE-R>>>"
        "$<BUILD_INTERFACE:$<$<COMPILE_LANGUAGE:CXX>:<memory$<ANGLE-R>>>"
        "$<BUILD_INTERFACE:$<$<COMPILE_LANGUAGE:CXX>:<string$<ANGLE-R>>>"
        "$<BUILD_INTERFACE:$<$<COMPILE_LANGUAGE:CXX>:<type_traits$<ANGLE-R>>>"
        "$<BUILD_INTERFACE:$<$<COMPILE_LANGUAGE:CXX>:<unordered_map$<ANGLE-R>>>"
        "$<BUILD_INTERFACE:$<$<COMPILE_LANGUAGE:CXX>:<utility$<ANGLE-R>>>"
        "$<BUILD_INTERFACE:$<$<COMPILE_LANGUAGE:CXX>:<vector$<ANGLE-R>>>"
    )
endmacro()
