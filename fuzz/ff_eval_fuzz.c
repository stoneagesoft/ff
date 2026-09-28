/*
 * libFuzzer entry point for ff_eval.
 *
 * Each invocation feeds the fuzzer's byte stream as Forth source to a
 * fresh interpreter instance, one line per ff_eval() call as ffsh and
 * ff_load() do, so state carried between calls is exercised too. Output
 * is dropped so the fuzzer is timing the engine, not the terminal.
 *
 * The engine runs as it would for untrusted code: `system`, the file
 * words and `load` are denied (a generated input must not run commands
 * or write files on the fuzzing machine), memory is capped, and a
 * watchdog stops endless loops.
 *
 * Build with FF_SAFE_MEM, as for any engine running untrusted code —
 * without it `0 @` is a crash by design:
 *   cmake -B fuzz/build -DFF_BUILD_FUZZ=ON -DFF_SAFE_MEM=ON \
 *       -DFF_BUILD_TESTS=OFF -DFF_BUILD_EXAMPLES=OFF \
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

/* Opcodes one input may run, and memory it may hold. */
#define FF_FUZZ_OPCODES    (250 * 1000)
#define FF_FUZZ_MEMORY     (16 * 1024 * 1024)


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

static ff_watchdog_action_t budget(void *ctx, uint64_t opcodes_run)
{
    (void)ctx;
    return opcodes_run >= FF_FUZZ_OPCODES ? FF_WD_ABORT : FF_WD_CONTINUE;
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
        .context           = NULL,
        .vprintf           = silent_vprintf,
        .vtracef           = silent_vtracef,
        .watchdog          = budget,
        .watchdog_interval = 4096,
        .deny              = FF_CAP_ALL,
        .mem_limit         = FF_FUZZ_MEMORY,
    };

    ff_t *ff = ff_new(&p);
    if (ff)
    {
        for (char *line = buf, *next; line; line = next)
        {
            next = strchr(line, '\n');
            if (next)
                *next++ = '\0';
            ff_eval(ff, line);
        }
        ff_free(ff);
    }

    free(buf);
    return 0;
}
