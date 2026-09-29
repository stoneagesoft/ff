/*
 * ff --- test driver.
 *
 * Runs a Forth source file through ff_eval() and compares captured
 * stdout against an expected-output file. Exit 0 on match, 1 on any
 * mismatch or I/O failure.
 *
 * Usage: ff_test_driver <input.ff> <expected.out>
 *
 * By default the whole file is one ff_eval() call, so evaluation stops
 * at the first error and the error itself is not reported. A test can
 * opt into a different mode with a directive on its first line (the
 * line is an ordinary `\` comment to the interpreter):
 *
 *     \ ff-test: lines budget=100000
 *
 *   lines     Feed each source line to its own ff_eval() call, the way
 *             ffsh and ff_load() do, so engine state must survive across
 *             calls. A line that fails appends "[NAME]" on a line of its
 *             own, NAME being its FF_ERR_* code without the prefix, and
 *             evaluation carries on with the next line.
 *   budget=N  Watchdog opcode budget (default 10 M).
 *   mem=N     Memory limit in bytes (ff_platform::mem_limit; default 256 MiB,
 *             so a runaway allocation fails the test instead of the machine).
 *   deny=N    Capabilities to withhold (ff_platform::deny, FF_CAP_* bits).
 */

#include <ff.h>
#include <ff_platform.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define BUF_SIZE (64 * 1024)


/* 10 M opcodes — well above any normal test, low enough to catch an
   infinite-loop bug in well under a second of wall-clock time. */
#define FF_TEST_OPCODE_BUDGET  (10ULL * 1000ULL * 1000ULL)

/* 256 MiB — far above any test's needs; a runaway allocation fails the
   test rather than exhausting the machine. */
#define FF_TEST_MEM_LIMIT      ((size_t)256 * 1024 * 1024)


typedef struct test_ctx
{
    char     *buf;
    size_t    size;
    size_t    capacity;
    uint64_t  budget;
} test_ctx_t;


/* Keyed by the enum constants rather than by position, so a new code
   can't silently shift every name after it. */
static const char *const FF_TEST_ERR_NAMES[] =
{
    [FF_OK]               = "OK",
    [FF_ERR_BAD_OPCODE]   = "BAD_OPCODE",
    [FF_ERR_BAD_PTR]      = "BAD_PTR",
    [FF_ERR_BROKEN]       = "BROKEN",
    [FF_ERR_DIV_ZERO]     = "DIV_ZERO",
    [FF_ERR_FILE_IO]      = "FILE_IO",
    [FF_ERR_FORGET_PROT]  = "FORGET_PROT",
    [FF_ERR_HEAP_OVER]    = "HEAP_OVER",
    [FF_ERR_MALFORMED]    = "MALFORMED",
    [FF_ERR_MISSING]      = "MISSING",
    [FF_ERR_NO_MAN]       = "NO_MAN",
    [FF_ERR_NON_UNIQUE]   = "NON_UNIQUE",
    [FF_ERR_NOT_IN_DEF]   = "NOT_IN_DEF",
    [FF_ERR_OOM]          = "OOM",
    [FF_ERR_RSTACK_OVER]  = "RSTACK_OVER",
    [FF_ERR_RSTACK_UNDER] = "RSTACK_UNDER",
    [FF_ERR_RUN_COMMENT]  = "RUN_COMMENT",
    [FF_ERR_RUN_STRING]   = "RUN_STRING",
    [FF_ERR_SCOPE_ARITY]  = "SCOPE_ARITY",
    [FF_ERR_SCOPE_OVER]   = "SCOPE_OVER",
    [FF_ERR_SCOPE_RSTACK] = "SCOPE_RSTACK",
    [FF_ERR_SCOPE_SIG]    = "SCOPE_SIG",
    [FF_ERR_STACK_OVER]   = "STACK_OVER",
    [FF_ERR_STACK_UNDER]  = "STACK_UNDER",
    [FF_ERR_UNDEFINED]    = "UNDEFINED",
    [FF_ERR_UNSUPPORTED]  = "UNSUPPORTED",
    [FF_ERR_ABORTED]      = "ABORTED",
    [FF_ERR_APPLICATION]  = "APPLICATION",
};

static const char *err_name(ff_error_t ec)
{
    if (ec < sizeof(FF_TEST_ERR_NAMES) / sizeof(FF_TEST_ERR_NAMES[0])
            && FF_TEST_ERR_NAMES[ec])
        return FF_TEST_ERR_NAMES[ec];
    return "UNKNOWN";
}


static ff_watchdog_action_t test_watchdog(void *ctx, uint64_t opcodes_run)
{
    const test_ctx_t *c = (const test_ctx_t *)ctx;
    return opcodes_run >= c->budget ? FF_WD_ABORT : FF_WD_CONTINUE;
}

static int capture_vprintf(void *ctx, const char *fmt, va_list args)
{
    test_ctx_t *c = (test_ctx_t *)ctx;
    size_t room = c->capacity - c->size;
    if (!room)
        return 0;

    va_list args_copy;
    va_copy(args_copy, args);
    int n = vsnprintf(c->buf + c->size, room, fmt, args_copy);
    va_end(args_copy);

    if (n < 0)
        return n;
    if ((size_t)n >= room)
        n = (int)room - 1;
    c->size += (size_t)n;
    return n;
}

static void capture_append(test_ctx_t *c, const char *s)
{
    size_t n = strlen(s);
    if (c->size + n + 1 > c->capacity)
        n = c->capacity - c->size - 1;
    memcpy(c->buf + c->size, s, n);
    c->size += n;
    c->buf[c->size] = '\0';
}

static char *read_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        fprintf(stderr, "cannot open %s: ", path);
        perror(NULL);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    if (size < 0)
    {
        fclose(f);
        return NULL;
    }
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf)
    {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = '\0';
    if (out_size)
        *out_size = got;
    return buf;
}

/* Parse the optional `\ ff-test:` directive on the first line. */
static void parse_directive(const char *src, bool *per_line, uint64_t *budget,
                            size_t *mem, uint32_t *deny)
{
    static const char tag[] = "\\ ff-test:";
    if (strncmp(src, tag, sizeof(tag) - 1) != 0)
        return;

    const char *p   = src + sizeof(tag) - 1;
    const char *eol = strchr(p, '\n');
    if (!eol)
        eol = p + strlen(p);

    while (p < eol)
    {
        while (p < eol && (*p == ' ' || *p == '\t'))
            ++p;
        const char *w = p;
        while (p < eol && *p != ' ' && *p != '\t')
            ++p;
        size_t len = (size_t)(p - w);

        if (len == 5 && strncmp(w, "lines", 5) == 0)
            *per_line = true;
        else if (len > 7 && strncmp(w, "budget=", 7) == 0)
            *budget = strtoull(w + 7, NULL, 10);
        else if (len > 4 && strncmp(w, "mem=", 4) == 0)
            *mem = (size_t)strtoull(w + 4, NULL, 10);
        else if (len > 5 && strncmp(w, "deny=", 5) == 0)
            *deny = (uint32_t)strtoul(w + 5, NULL, 0);
    }
}

/* Put a failure in the output as `[NAME]` on a line of its own, so the
   expected output pins down which error ended the input — not only what
   was printed before it. */
static void record_error(test_ctx_t *ctx, ff_error_t ec)
{
    if (ctx->size && ctx->buf[ctx->size - 1] != '\n')
        capture_append(ctx, "\n");
    capture_append(ctx, "[");
    capture_append(ctx, err_name(ec));
    capture_append(ctx, "]\n");
}

/* Evaluate @p src one line at a time, recording each failure. */
static ff_error_t eval_lines(ff_t *ff, test_ctx_t *ctx, char *src)
{
    ff_error_t last = FF_OK;
    char *line = src;
    while (*line)
    {
        char *eol = strchr(line, '\n');
        char *next = eol ? eol + 1 : line + strlen(line);
        if (eol)
            *eol = '\0';

        ff_error_t ec = ff_eval(ff, line);
        if (ec != FF_OK)
        {
            record_error(ctx, ec);
            last = ec;
        }
        line = next;
    }
    return last;
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <input.ff> <expected.out>\n", argv[0]);
        return 2;
    }

    const char *input_path = argv[1];
    const char *expect_path = argv[2];

    size_t src_size = 0;
    char *src = read_file(input_path, &src_size);
    if (!src)
        return 2;

    size_t expect_size = 0;
    char *expect = read_file(expect_path, &expect_size);
    if (!expect)
    {
        free(src);
        return 2;
    }

    bool per_line = false;
    uint64_t budget = FF_TEST_OPCODE_BUDGET;
    size_t mem = FF_TEST_MEM_LIMIT;
    uint32_t deny = 0;
    parse_directive(src, &per_line, &budget, &mem, &deny);

    test_ctx_t ctx =
    {
        .buf      = (char *)calloc(1, BUF_SIZE),
        .size     = 0,
        .capacity = BUF_SIZE,
        .budget   = budget,
    };

    /* Generous watchdog so a runaway test (or a 011_watchdog-style
       infinite loop test) can't hang ctest. 10 million opcodes is
       comfortably above any well-behaved test's footprint. */
    ff_platform_t p =
    {
        .context           = &ctx,
        .vprintf           = capture_vprintf,
        .vtracef           = NULL,
        .watchdog          = test_watchdog,
        .watchdog_interval = 65536,
        .deny              = deny,
        .mem_limit         = mem,
    };

    ff_t *ff = ff_new(&p);
    ff_error_t ec = per_line ? eval_lines(ff, &ctx, src) : ff_eval(ff, src);
    /* A whole-file case that ends in an error records it too: otherwise
       a test of an error path passes when the error never happens, as
       long as the output before it matches. */
    if (!per_line && ec != FF_OK)
        record_error(&ctx, ec);
    ff_free(ff);

    int rc = 0;
    if (ctx.size != expect_size || memcmp(ctx.buf, expect, expect_size) != 0)
    {
        fprintf(stderr, "=== %s ===\n", input_path);
        fprintf(stderr, "ff_eval returned %u\n", (unsigned)ec);
        fprintf(stderr, "EXPECTED (%zu bytes):\n%.*s\n",
                expect_size, (int)expect_size, expect);
        fprintf(stderr, "GOT (%zu bytes):\n%.*s\n",
                ctx.size, (int)ctx.size, ctx.buf);
        rc = 1;
    }

    free(ctx.buf);
    free(expect);
    free(src);
    return rc;
}
