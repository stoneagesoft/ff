/*
 * libFuzzer entry point for ff_eval.
 *
 * Each invocation feeds the fuzzer's byte stream as Forth source to a
 * fresh interpreter instance, one line per ff_eval() call as ffsh and
 * ff_load() do, so state carried between calls is exercised too. After
 * a blank line the host releases the transient strings, as a long-running
 * host does between calls; a program still holding one must be refused.
 * A few native words call back into the engine the way hosts do (see
 * host_words). Output is formatted, so that the arguments the engine
 * passes are read, and then dropped: the fuzzer is timing the engine,
 * not the terminal.
 *
 * The engine runs as it would for untrusted code: `system`, the file
 * words and `load` are denied (a generated input must not run commands
 * or write files on the fuzzing machine), memory is capped, and a
 * watchdog stops endless loops.
 *
 * Build with Clang and FF_SAFE_MEM, as for any engine running untrusted
 * code — without it `0 @` is a crash by design. FF_BUILD_FUZZ adds the
 * coverage and sanitizer flags itself (ASan and UBSan, every report
 * fatal):
 *   cmake -B build/fuzz -DFF_BUILD_FUZZ=ON -DFF_SAFE_MEM=ON \
 *       -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=RelWithDebInfo
 *   cmake --build build/fuzz
 *
 * The build also writes build/fuzz/ff.dict, the name of every word the
 * harness has, as a libFuzzer dictionary. Run, with the test cases as
 * the seed corpus (-jobs=N -workers=N runs N at once):
 *   mkdir -p build/fuzz/corpus
 *   cp test/cases/[0-9]*.ff build/fuzz/corpus
 *   build/fuzz/ff_eval_fuzz -dict=build/fuzz/ff.dict build/fuzz/corpus
 *
 * Compiled with FF_FUZZ_DICT, this file is instead the program that
 * writes the dictionary.
 */

#include <ff.h>
#include <ff_platform.h>

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* The input may not be NUL-terminated; ff_eval expects a C string, so
   copy into a heap buffer and null-terminate. Cap at 64 KiB to keep
   each iteration fast — corpus growth past that point is unlikely to
   surface new bugs in the tokenizer or inner interpreter. */
#define FF_FUZZ_MAX_INPUT  (64 * 1024)

/* Opcodes one input may run, all its lines together, and memory it may
   hold. The engine counts opcodes afresh for each ff_eval(), so the
   budget counts watchdog calls instead: one per FF_FUZZ_WD_INTERVAL
   opcodes. Counted per line, as it was, an input of a few hundred lines
   ran for seconds, and the fuzzer managed ten inputs a second. */
#define FF_FUZZ_OPCODES     (250 * 1000)
#define FF_FUZZ_WD_INTERVAL 4096
#define FF_FUZZ_MEMORY      (16 * 1024 * 1024)

/* Watchdog calls the current input has used. */
static unsigned g_wd_calls;


static int silent_vprintf(void *ctx, const char *fmt, va_list args)
{
    (void)ctx;
    char buf[256];
    return vsnprintf(buf, sizeof(buf), fmt, args);
}

static int silent_vtracef(void *ctx, ff_error_t e, const char *fmt, va_list args)
{
    (void)e;
    return silent_vprintf(ctx, fmt, args);
}

static ff_watchdog_action_t budget(void *ctx, uint64_t opcodes_run)
{
    (void)ctx;
    (void)opcodes_run;
    return ++g_wd_calls >= FF_FUZZ_OPCODES / FF_FUZZ_WD_INTERVAL
               ? FF_WD_ABORT : FF_WD_CONTINUE;
}


/* ( s -- ... )  Run string s through a nested ff_eval(). */
static void host_eval(ff_t *ff)
{
    const char *s;
    if (ff_pop_str(ff, &s))
        ff_eval(ff, s);
}

/* ( s -- ... )  Look the word named s up and run it with ff_exec(). */
static void host_exec(ff_t *ff)
{
    const char *s;
    if (ff_pop_str(ff, &s))
        ff_exec(ff, ff_find(ff, s));
}

/* ( s -- s' )  A copy of string s, made by ff_push_str(). */
static void host_copy(ff_t *ff)
{
    const char *s;
    if (ff_pop_str(ff, &s))
        ff_push_str(ff, s, strlen(s));
}

/* ( n -- )  Throw n with ff_throwf(). */
static void host_throw(ff_t *ff)
{
    int64_t n;
    if (ff_pop_int(ff, &n))
        ff_throwf(ff, n, "Host threw %" PRId64 ".", n);
}

static const ff_native_word_t host_words[] =
{
    FF_NATIVE("host-eval", host_eval, NULL),
    FF_NATIVE("host-exec", host_exec, NULL),
    FF_NATIVE("host-copy", host_copy, NULL),
    FF_NATIVE("host-throw", host_throw, NULL),
    FF_NATIVE_END
};

/* An engine set up as the fuzzer runs it, printing through @p out. */
static ff_t *fuzz_engine(ff_vprintf_fn out, void *ctx)
{
    ff_platform_t p =
    {
        .context           = ctx,
        .vprintf           = out,
        .vtracef           = silent_vtracef,
        .watchdog          = budget,
        .watchdog_interval = FF_FUZZ_WD_INTERVAL,
        .deny              = FF_CAP_ALL,
        .mem_limit         = FF_FUZZ_MEMORY,
    };

    ff_t *ff = ff_new(&p);
    if (ff && ff_register(ff, host_words) != FF_OK)
    {
        ff_free(ff);
        ff = NULL;
    }
    return ff;
}


#ifndef FF_FUZZ_DICT

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > FF_FUZZ_MAX_INPUT)
        size = FF_FUZZ_MAX_INPUT;

    char *buf = (char *)malloc(size + 1);
    if (!buf)
        return 0;
    memcpy(buf, data, size);
    buf[size] = '\0';

    g_wd_calls = 0;
    ff_t *ff = fuzz_engine(silent_vprintf, NULL);
    if (ff)
    {
        for (char *line = buf, *next;
             line && g_wd_calls < FF_FUZZ_OPCODES / FF_FUZZ_WD_INTERVAL;
             line = next)
        {
            next = strchr(line, '\n');
            if (next)
                *next++ = '\0';
            if (*line)
                ff_eval(ff, line);
            else
                ff_release_strings(ff);
        }
        ff_free(ff);
    }

    free(buf);
    return 0;
}

#else

/* The output of `words`, collected. */
typedef struct
{
    char  *text;
    size_t len, cap;
} fuzz_text_t;

static int collect_vprintf(void *ctx, const char *fmt, va_list args)
{
    fuzz_text_t *t = (fuzz_text_t *)ctx;
    va_list again;
    va_copy(again, args);
    int n = vsnprintf(NULL, 0, fmt, again);
    va_end(again);
    if (n < 0)
        return n;
    if (t->len + (size_t)n + 1 > t->cap)
    {
        size_t cap = 2 * (t->len + (size_t)n + 1);
        char *text = (char *)realloc(t->text, cap);
        if (!text)
            return -1;
        t->text = text;
        t->cap = cap;
    }
    vsnprintf(t->text + t->len, (size_t)n + 1, fmt, args);
    t->len += (size_t)n;
    return n;
}

/* One dictionary entry: @p len bytes of @p s, quoted — and for a word,
   padded with a blank either side, so that where the fuzzer inserts it
   it reads as a token of its own. */
static void put_entry(FILE *f, const char *s, size_t len, bool word)
{
    fputs(word ? "\" " : "\"", f);
    for (size_t i = 0; i < len; ++i)
    {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\')
            fprintf(f, "\\%c", c);
        else if (isprint(c))
            fputc(c, f);
        else
            fprintf(f, "\\x%02X", c);
    }
    fputs(word ? " \"\n" : "\"\n", f);
}

/* Write the libFuzzer dictionary: every word the harness's engine knows,
   and the tokens that aren't words — to the file named by argv[1]. */
int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s DICT\n", argv[0]);
        return EXIT_FAILURE;
    }

    fuzz_text_t words = { NULL, 0, 0 };
    ff_t *ff = fuzz_engine(collect_vprintf, &words);
    if (!ff || ff_eval(ff, "words") != FF_OK || !words.text)
    {
        fprintf(stderr, "%s: can't list the words.\n", argv[0]);
        return EXIT_FAILURE;
    }
    ff_free(ff);

    FILE *f = fopen(argv[1], "w");
    if (!f)
    {
        perror(argv[1]);
        return EXIT_FAILURE;
    }

    /* Pieces of literals and comments, which aren't words. */
    static const char *const syntax[] =
    {
        "\"", "\\x", "\\u", " ( ", ")", " \\ ", "0x", "-", ".", "e-"
    };
    fputs("# libFuzzer dictionary for ff_eval_fuzz; generated, do not edit.\n", f);
    for (size_t i = 0; i < sizeof(syntax) / sizeof(syntax[0]); ++i)
        put_entry(f, syntax[i], strlen(syntax[i]), false);

    for (const char *p = words.text; *p; )
    {
        while (isspace((unsigned char)*p))
            ++p;
        const char *start = p;
        while (*p && !isspace((unsigned char)*p))
            ++p;
        if (p > start)
            put_entry(f, start, (size_t)(p - start), true);
    }

    free(words.text);
    return fclose(f) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

#endif
