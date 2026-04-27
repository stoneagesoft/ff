# libFuzzer harness for ff_eval. Only built when explicitly enabled
# (it requires Clang and the libFuzzer / sanitizer flags to be in
# scope already; see fuzz/ff_eval_fuzz.c for an invocation example).

add_executable(ff_eval_fuzz ff_eval_fuzz.c)
target_link_libraries(ff_eval_fuzz PRIVATE ff::ff)
