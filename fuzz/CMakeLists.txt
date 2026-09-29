# libFuzzer harness for ff_eval (see ff_eval_fuzz.c for building and
# running it). Built with -DFF_BUILD_FUZZ=ON, which is the whole of the
# configuration: the engine is instrumented here, for coverage and for
# ASan and UBSan, so no sanitizer flags go on the command line.

if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "FF_BUILD_FUZZ needs Clang, for libFuzzer "
                        "(-DCMAKE_C_COMPILER=clang).")
endif()
if(NOT FF_SAFE_MEM)
    message(FATAL_ERROR "FF_BUILD_FUZZ needs -DFF_SAFE_MEM=ON: the fuzzer "
                        "runs arbitrary Forth, and without it `0 @` is a "
                        "crash by design.")
endif()

# Every report is fatal, so libFuzzer keeps the input. Forth arithmetic
# wraps (-fwrapv), so a signed overflow is no bug, whatever a Clang's
# -fsanitize=undefined makes of it.
set(_ff_fuzz_sanitize
    -fsanitize=address,undefined
    -fno-sanitize=signed-integer-overflow
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
    -g)

target_compile_options(ff_obj PRIVATE -fsanitize=fuzzer-no-link ${_ff_fuzz_sanitize})

# libFuzzer supplies main().
add_executable(ff_eval_fuzz ff_eval_fuzz.c)
target_compile_options(ff_eval_fuzz PRIVATE -fsanitize=fuzzer ${_ff_fuzz_sanitize})
target_link_options(ff_eval_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
target_link_libraries(ff_eval_fuzz PRIVATE ff::ff)

# The harness again, with a main() of its own that writes the names of
# its words as a libFuzzer dictionary: ff.dict, next to the fuzzer.
add_executable(ff_fuzz_dict ff_eval_fuzz.c)
target_compile_definitions(ff_fuzz_dict PRIVATE FF_FUZZ_DICT)
target_compile_options(ff_fuzz_dict PRIVATE ${_ff_fuzz_sanitize})
target_link_options(ff_fuzz_dict PRIVATE -fsanitize=address,undefined)
target_link_libraries(ff_fuzz_dict PRIVATE ff::ff)

add_custom_command(
    OUTPUT "${PROJECT_BINARY_DIR}/ff.dict"
    COMMAND ff_fuzz_dict "${PROJECT_BINARY_DIR}/ff.dict"
    DEPENDS ff_fuzz_dict
    COMMENT "Writing the fuzzing dictionary ff.dict"
    VERBATIM)
add_custom_target(ff_fuzz_dict_file ALL DEPENDS "${PROJECT_BINARY_DIR}/ff.dict")
