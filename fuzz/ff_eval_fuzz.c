/*
 * libFuzzer entry point for ff_eval.
 *
 * Each invocation feeds the fuzzer's byte stream as Forth source to a
 * fresh interpreter instance. Output is dropped (no platform
 * callbacks) so the fuzzer is timing the engine, not the terminal.
 *
 * Build with:
 *   cmake -B fuzz/build -DFF_BUILD_FUZZ=ON \
 *       -DCMAKE_C_COMPILER=clang \
 *       -DCMAKE_C_FLAGS="-fsanitize=fuzzer,address,undefined -O1"
 *   cmake --build fuzz/build
 *
 * Run with:
 *   fuzz/build/ff_eval_fuzz fuzz/corpus
 */

#include <ff.h>
#include <ff_platform.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


/* The input may not be NUL-terminated; ff_eval expects a C string, so
   copy into a heap buffer and null-terminate. Cap at 64 KiB to keep
   each iteration fast — corpus growth past that point is unlikely to
   surface new bugs in the tokenizer or inner interpreter. */
#define FF_FUZZ_MAX_INPUT  (64 * 1024)


static int silent_vprintf(void *ctx, const char *fmt, va_list args)
{
    (void)ctx; (void)fmt; (void)args;
    return 0;
}

static int silent_vtracef(void *ctx, ff_error_t e, const char *fmt, va_list args)
{
    (void)ctx; (void)e; (void)fmt; (void)args;
    return 0;
}


int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > FF_FUZZ_MAX_INPUT)
        size = FF_FUZZ_MAX_INPUT;

    char *buf = (char *)malloc(size + 1);
    if (!buf)
        return 0;
    memcpy(buf, data, size);
    buf[size] = '\0';

    ff_platform_t p =
    {
        .context = NULL,
        .vprintf = silent_vprintf,
        .vtracef = silent_vtracef,
    };

    ff_t *ff = ff_new(&p);
    if (ff)
    {
        ff_eval(ff, buf);
        ff_free(ff);
    }

    free(buf);
    return 0;
}
