/*
 * ff --- API-level regression tests.
 *
 * Covers what a script test can't observe: the state an error leaves on
 * the return stack and the scope barrier, the location reported for an
 * error, ff_load()'s line handling, the watchdog / abort flag across
 * nested evaluations, errors raised by native words, ff_exec() called
 * directly by a host, what a definition that fails leaves in the
 * dictionary, the memory limit and the host's control over file and
 * command access. It includes <ff_p.h> the way a native-word author
 * does, and is built with strict warnings, so it also checks that the
 * private headers compile cleanly.
 *
 * Scratch files are written to, and removed from, the working directory.
 *
 * Usage: ff_api_test   (exit 0 when every check passes)
 */

#include <ff_p.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int g_failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) \
        { \
            fprintf(stderr, "%s:%d: check failed: %s\n", \
                    __FILE__, __LINE__, #cond); \
            ++g_failures; \
        } \
    } while (0)


/* Output of the engine under test, and its watchdog budget. */
static char     g_out[4096];
static size_t   g_out_len;
static uint64_t g_budget;

static void reset_output(void)
{
    g_out_len = 0;
    g_out[0] = '\0';
}

static int capture_vprintf(void *ctx, const char *fmt, va_list args)
{
    (void)ctx;
    size_t room = sizeof(g_out) - g_out_len;
    int n = vsnprintf(g_out + g_out_len, room, fmt, args);
    if (n > 0)
        g_out_len += (size_t)n < room ? (size_t)n : room - 1;
    return n;
}

static ff_watchdog_action_t budget_watchdog(void *ctx, uint64_t opcodes_run)
{
    (void)ctx;
    return opcodes_run >= g_budget ? FF_WD_ABORT : FF_WD_CONTINUE;
}

static ff_t *new_engine(uint64_t budget)
{
    ff_platform_t p =
    {
        .vprintf           = capture_vprintf,
        .watchdog          = budget_watchdog,
        .watchdog_interval = 4096,
    };
    g_budget = budget;
    reset_output();
    return ff_new(&p);
}

static void write_file(const char *name, const char *text)
{
    FILE *f = fopen(name, "w");
    if (!f)
    {
        perror(name);
        exit(2);
    }
    fputs(text, f);
    fclose(f);
}


/* An error abandons the return frames and scope barrier it was raised
   under. Before, each failing call leaked return-stack cells until the
   array overflowed, and a failed scope kept its barrier raised. */
static void test_state_after_errors(void)
{
    ff_t *ff = new_engine(10000000);

    ff_eval(ff, ": x drop ;");
    for (int i = 0; i < 1000; ++i)
        CHECK(ff_eval(ff, "x") == FF_ERR_STACK_UNDER);
    CHECK(ff->r_stack.top == 0);

    ff_eval(ff, ": f { ( a -- b ) drop drop } ;");
    for (int i = 0; i < 1000; ++i)
    {
        CHECK(ff_eval(ff, "1 2 f") == FF_ERR_STACK_UNDER);
        ff_eval(ff, "clear");
    }
    CHECK(ff->r_stack.top == 0);
    CHECK(ff->stack.floor == 0);
    CHECK(ff->n_scopes == 0);

    reset_output();
    CHECK(ff_eval(ff, "1 2 + .") == FF_OK);
    CHECK(strcmp(g_out, "3") == 0);

    ff_free(ff);
}

/* ff_err_line() / ff_err_pos() locate the offending token: the position
   was never recorded, and ff_load() lines were numbered from 0. */
static void test_error_location(void)
{
    ff_t *ff = new_engine(10000000);

    CHECK(ff_eval(ff, "1 2 zork") == FF_ERR_UNDEFINED);
    CHECK(ff_err_line(ff) == 1);
    CHECK(ff_err_pos(ff) == 4);

    CHECK(ff_eval(ff, "1 2 +\n  3 4 zork 5") == FF_ERR_UNDEFINED);
    CHECK(ff_err_line(ff) == 2);
    CHECK(ff_err_pos(ff) == 6);

    /* A runtime error points at the word that was running. */
    ff_eval(ff, ": boom 0 / ;");
    CHECK(ff_eval(ff, "7 7 1 boom") == FF_ERR_DIV_ZERO);
    CHECK(ff_err_line(ff) == 1);
    CHECK(ff_err_pos(ff) == 6);

#if FF_WITH_FILES
    /* Files count lines from 1, and a nested `load` hands its caller's
       line count back intact for an error later on the same line. */
    write_file("api_inner.ff", "3 drop\n");
    write_file("api_outer.ff",
               "1 drop\n"
               "2 drop\n"
               "\"api_inner.ff\" load drop zork\n");
    CHECK(ff_load(ff, "api_outer.ff") == FF_ERR_UNDEFINED);
    CHECK(ff_err_line(ff) == 3);
    CHECK(ff_err_pos(ff) == 25);
    remove("api_inner.ff");
    remove("api_outer.ff");
#endif

    ff_free(ff);
}

/* ff_load() used a 4 KiB line buffer and split longer lines mid-token.
   The line here is ~10 KB and mostly multi-digit tokens, so a split at
   any buffer boundary changes the sum or leaves pieces on the stack. */
static void test_load_long_line(void)
{
    ff_t *ff = new_engine(10000000);

    enum { TERMS = 1000 };
    static const char term[] = " 1234567 +";
    char *text = (char *)malloc(TERMS * (sizeof(term) - 1) + 16);
    strcpy(text, "0");
    for (int i = 0; i < TERMS; ++i)
        strcat(text, term);
    strcat(text, " .\n");
    write_file("api_long.ff", text);
    free(text);

    reset_output();
    CHECK(ff_load(ff, "api_long.ff") == FF_OK);
    CHECK(strcmp(g_out, "1234567000") == 0);
    CHECK(ff_depth(ff) == 0);
    remove("api_long.ff");

    ff_free(ff);
}

static void test_load_errors(void)
{
    ff_t *ff = new_engine(10000000);

    /* A bare code, like every other API return, not severity | code. */
    CHECK(ff_load(ff, "api_no_such_file.ff") == FF_ERR_FILE_IO);

    /* A runaway comment is reported, then dropped: left set, it swallowed
       whatever the caller evaluated next. */
    write_file("api_comment.ff", "1 drop ( never closed\n");
    CHECK(ff_load(ff, "api_comment.ff") == FF_ERR_RUN_COMMENT);
    reset_output();
    CHECK(ff_eval(ff, "2 .") == FF_OK);
    CHECK(strcmp(g_out, "2") == 0);
    remove("api_comment.ff");

    ff_free(ff);
}

#if FF_WITH_FILES
/* A loop that re-enters the interpreter through `load` is still bounded:
   nested evaluations no longer restart the watchdog's count. */
static void test_watchdog_across_load(void)
{
    ff_t *ff = new_engine(20000);

    write_file("api_tiny.ff", "1 drop\n");
    ff_eval(ff, ": spin begin \"api_tiny.ff\" load drop again ;");
    CHECK(ff_eval(ff, "spin") == FF_ERR_ABORTED);
    remove("api_tiny.ff");

    ff_free(ff);
}
#endif

static void req_abort(ff_t *ff)
{
    ff_request_abort(ff);
}

static void nat_fail(ff_t *ff)
{
    FF_SL(ff, 1);
    ff_stack_pop(&ff->stack);
}

static void nat_abort(ff_t *ff)
{
    ff_abort(ff);
}

/* An error raised inside a native word stops the word that called it and
   reaches `catch` like any other; before, the caller ran on. ff_abort()
   from inside a native unwinds first and resets only at the top, instead
   of tearing the engine down under its callers. */
static void test_native_errors(void)
{
    ff_t *ff = new_engine(10000000);

    static const ff_native_word_t words[] =
    {
        FF_NATIVE("nat-fail", nat_fail, NULL),
        FF_NATIVE("nat-abort", nat_abort, NULL),
        FF_NATIVE_END
    };
    ff_register(ff, words);

    ff_eval(ff, ": t nat-fail 99 . ;");
    reset_output();
    CHECK(ff_eval(ff, "t") == FF_ERR_STACK_UNDER);
    CHECK(strcmp(g_out, "") == 0);

    reset_output();
    CHECK(ff_eval(ff, "' nat-fail catch .") == FF_OK);
    CHECK(strcmp(g_out, "-4") == 0);

    ff_eval(ff, ": t2 1 2 nat-abort 3 . ;");
    reset_output();
    CHECK(ff_eval(ff, "t2") == FF_ERR_ABORTED);
    CHECK(strcmp(g_out, "") == 0);
    CHECK(ff_depth(ff) == 0);

    ff_free(ff);
}

/* A host calling ff_exec() directly gets false for a failed run, and the
   engine is left clean: nothing is still unwinding into the next call. */
static void test_host_exec(void)
{
    ff_t *ff = new_engine(10000000);

    ff_eval(ff, ": bad drop ;  : ab abort ;");
    CHECK(!ff_exec(ff, ff_dict_lookup(&ff->dict, "bad")));
    CHECK(ff_errno(ff) == FF_ERR_STACK_UNDER);
    CHECK(!(ff->state & FF_STATE_THROWN));
    CHECK(ff->exec_depth == 0);

    CHECK(ff_push_int(ff, 5));
    CHECK(ff_exec(ff, ff_dict_lookup(&ff->dict, "dup")));
    CHECK(ff_depth(ff) == 2);

    /* Words that parse have no input to parse here: `see` and `parse-word`
       read through a NULL pointer. */
    CHECK(!ff_exec(ff, ff_dict_lookup(&ff->dict, "see")));
    CHECK(ff_errno(ff) == FF_ERR_MISSING);
    size_t depth = ff_depth(ff);
    CHECK(ff_exec(ff, ff_dict_lookup(&ff->dict, "parse-word")));
    CHECK(ff_depth(ff) == depth + 1);

    /* An uncaught ABORT resets the engine at the host boundary too. */
    CHECK(!ff_exec(ff, ff_dict_lookup(&ff->dict, "ab")));
    CHECK(ff_errno(ff) == FF_ERR_ABORTED);
    CHECK(ff_depth(ff) == 0);

    reset_output();
    CHECK(ff_eval(ff, "1 2 + .") == FF_OK);
    CHECK(strcmp(g_out, "3") == 0);

    ff_free(ff);
}

/* A definition that never reaches `;` leaves nothing in the dictionary:
   not one an error ended, not one the host abandons with ff_abort(), and
   no word at all when the name is missing. Each used to stay behind,
   half-built or nameless. */
static void test_definitions(void)
{
    ff_t *ff = new_engine(10000000);
    size_t base = ff->dict.count;

    CHECK(ff_eval(ff, ": half 1 2 zork ;") == FF_ERR_UNDEFINED);
    CHECK(ff_eval(ff, ": 42") == FF_ERR_MISSING);
    CHECK(ff_eval(ff, "create 42") == FF_ERR_MISSING);
    CHECK(ff_eval(ff, "5 constant \"x\"") == FF_ERR_MISSING);
    CHECK(ff_eval(ff, "variable") == FF_ERR_MISSING);
    CHECK(ff->dict.count == base);
    CHECK(ff->compiling == NULL);
    CHECK(!(ff->state & FF_STATE_COMPILING));

    /* Left open across calls, then abandoned by the host. */
    CHECK(ff_eval(ff, ": open 1 2") == FF_OK);
    CHECK(ff->state & FF_STATE_COMPILING);
    ff_abort(ff);
    CHECK(ff->dict.count == base);
    CHECK(ff->compiling == NULL);
    CHECK(!(ff->state & FF_STATE_COMPILING));

    /* A failing ff_exec() of the host's own is not part of the input a
       definition is being compiled from, and leaves it open. */
    CHECK(ff_eval(ff, ": open2 1") == FF_OK);
    CHECK(!ff_exec(ff, ff_dict_lookup(&ff->dict, "drop")));
    CHECK(ff_eval(ff, "2 ;") == FF_OK);
    reset_output();
    CHECK(ff_eval(ff, "open2 + .") == FF_OK);
    CHECK(strcmp(g_out, "3") == 0);
    CHECK(ff->dict.count == base + 1);

    /* A definition a loaded file began goes with the load's own error. */
    write_file("api_open.ff", ": in-file 1 2 ( runaway\n");
    CHECK(ff_load(ff, "api_open.ff") == FF_ERR_RUN_COMMENT);
    CHECK(ff->compiling == NULL);
    CHECK(ff->dict.count == base + 1);
    remove("api_open.ff");

    /* `abort"` at the prompt raises with its own message; it used to read
       one from past the end of its execution stub. */
    CHECK(ff_eval(ff, "abort\" \"halt here\"") == FF_ERR_ABORTED);
    CHECK(strcmp(ff_strerror(ff), "halt here") == 0);

    /* Freed with a definition and a `{` scope still open: the scope's
       input names went unfreed (LeakSanitizer reports it). */
    CHECK(ff_eval(ff, ": still-open { ( a -- b ) a") == FF_OK);

    ff_free(ff);
}

/* A memory limit (ff_platform::mem_limit) caps what Forth code can make
   the engine hold, and what `forget` or a failed definition drops comes
   off the account again. Before, sizes weren't checked (`-1 array`
   crashed) and nothing was ever given back. */
static void test_memory_limit(void)
{
    ff_platform_t p = { .vprintf = capture_vprintf, .mem_limit = 100000 };
    ff_t *ff = ff_new(&p);
    size_t base = ff->dict.mem.used;

    CHECK(ff_eval(ff, "create x 20000 allot") == FF_ERR_HEAP_OVER);
    CHECK(ff->dict.mem.used <= 100000);
    CHECK(ff_eval(ff, "-1 array a") == FF_ERR_MALFORMED);
    CHECK(ff_eval(ff, "clear forget x") == FF_OK);
    CHECK(ff->dict.mem.used == base);

    /* Defining and forgetting, over and over, holds steady. */
    CHECK(ff_eval(ff, ": w 1 2 3 4 5 6 7 8 9 ; forget w") == FF_OK);
    size_t steady = ff->dict.mem.used;
    for (int i = 0; i < 2000; ++i)
        CHECK(ff_eval(ff, ": w 1 2 3 4 5 6 7 8 9 ; forget w") == FF_OK);
    CHECK(ff->dict.mem.used == steady);

    /* Interpreted strings are kept for the engine's lifetime, so they
       count too: the limit stops them, and ff_abort() gives them back. */
    ff_error_t ec = FF_OK;
    for (int i = 0; i < 100000 && ec == FF_OK; ++i)
        ec = ff_eval(ff, "\"0123456789012345678901234567890123456789\" drop");
    CHECK(ec == FF_ERR_HEAP_OVER);
    CHECK(ff->dict.mem.used <= 100000);
    ff_abort(ff);
    CHECK(ff->dict.mem.used == steady);
    CHECK(ff_eval(ff, "\"still room\" drop") == FF_OK);

    ff_free(ff);
}

#if FF_WITH_SYSTEM && FF_WITH_FILES
static int  g_commands;
static char g_last_command[64];

static int record_command(void *ctx, const char *cmd)
{
    (void)ctx;
    ++g_commands;
    snprintf(g_last_command, sizeof(g_last_command), "%s", cmd);
    return 7;
}

/* Opens only "virtual.ff", backed by a real file. */
static FILE *open_virtual(void *ctx, const char *path, const char *mode)
{
    (void)ctx;
    if (strcmp(path, "virtual.ff") == 0)
        return fopen("api_virtual.ff", mode);
    errno = EACCES;
    return NULL;
}

/* The host decides what Forth code reaches outside the engine: `system`
   and file access go through ff_platform::run_command / open_file when it
   sets them, ff_platform::deny withholds them, and streams a program
   leaves open are closed with the engine. */
static void test_sandbox(void)
{
    write_file("api_virtual.ff", "40 2 +\n");

    ff_platform_t p =
    {
        .vprintf     = capture_vprintf,
        .open_file   = open_virtual,
        .run_command = record_command,
    };
    ff_t *ff = ff_new(&p);

    reset_output();
    CHECK(ff_eval(ff, "\"echo hi\" system .") == FF_OK);
    CHECK(strcmp(g_out, "7") == 0);
    CHECK(g_commands == 1 && strcmp(g_last_command, "echo hi") == 0);

    reset_output();
    CHECK(ff_eval(ff, "\"virtual.ff\" load . 32 emit .") == FF_OK);
    CHECK(strcmp(g_out, "0 42") == 0);
    reset_output();
    CHECK(ff_eval(ff, "\"api_virtual.ff\" load .") == FF_OK);
    CHECK(strcmp(g_out, "-37") == 0);
    CHECK(ff_eval(ff, "\"r\" \"virtual.ff\" fopen fclose drop") == FF_OK);
    CHECK(ff_load(ff, "virtual.ff") == FF_OK);
    ff_free(ff);

    /* Denied, the words raise -21 — and the host's own ff_load() works. */
    ff_platform_t q = { .vprintf = capture_vprintf, .deny = FF_CAP_ALL };
    ff = ff_new(&q);
    CHECK(ff_eval(ff, "\"echo hi\" system") == FF_ERR_UNSUPPORTED);
    CHECK(ff_eval(ff, "clear \"api_virtual.ff\" load") == FF_ERR_UNSUPPORTED);
    CHECK(ff_eval(ff, "clear \"r\" \"api_virtual.ff\" fopen") == FF_ERR_UNSUPPORTED);
    CHECK(ff_eval(ff, "clear stdout") == FF_ERR_UNSUPPORTED);
    CHECK(ff_load(ff, "api_virtual.ff") == FF_OK);
    ff_free(ff);
    remove("api_virtual.ff");

    /* A stream left open is closed, and so flushed, when the engine is. */
    ff_platform_t r = { .vprintf = capture_vprintf };
    ff = ff_new(&r);
    CHECK(ff_eval(ff, "\"w\" \"api_unclosed.txt\" fopen \"kept\" fputs drop") == FF_OK);
    ff_free(ff);
    FILE *f = fopen("api_unclosed.txt", "r");
    char buf[16] = { 0 };
    CHECK(f && fgets(buf, sizeof(buf), f) && strcmp(buf, "kept") == 0);
    if (f)
        fclose(f);
    remove("api_unclosed.txt");
}
#endif

#if FF_WITH_FILES
/* `load` pushes the THROW code that ended it, like `evaluate`, and QUIT
   in a loaded file ends the load without an error. */
static void test_load_codes(void)
{
    ff_t *ff = new_engine(10000000);

    reset_output();
    CHECK(ff_eval(ff, "\"api_no_such_file.ff\" load .") == FF_OK);
    CHECK(strcmp(g_out, "-37") == 0);

    write_file("api_div.ff", "1 0 /\n2 .\n");
    reset_output();
    CHECK(ff_eval(ff, "\"api_div.ff\" load .") == FF_OK);
    CHECK(strcmp(g_out, "-10") == 0);
    remove("api_div.ff");

    write_file("api_quit.ff", "1 quit 2\n3\n");
    ff_eval(ff, "clear");
    CHECK(ff_load(ff, "api_quit.ff") == FF_OK);
    CHECK(ff_depth(ff) == 1);
    remove("api_quit.ff");

    ff_free(ff);
}
#endif

/* An abort requested while nested evaluations run is honoured, not
   cleared by the next nested ff_eval entry. */
static void test_abort_request_nested(void)
{
    ff_t *ff = new_engine(50000000);    /* safety net only */

    static const ff_native_word_t words[] =
    {
        FF_NATIVE("req-abort", req_abort, NULL),
        FF_NATIVE_END
    };
    ff_register(ff, words);

    ff_eval(ff, ": spin req-abort begin \"1 drop\" evaluate drop again ;");
    CHECK(ff_eval(ff, "spin") == FF_ERR_ABORTED);
    /* The flag is polled every few hundred back-branches; only a lost
       request would run on towards the safety-net budget. */
    CHECK(ff->opcodes_run < 100000);
    CHECK(ff_eval(ff, "1 drop") == FF_OK);

    ff_free(ff);
}

int main(void)
{
    test_state_after_errors();
    test_error_location();
    test_load_long_line();
    test_load_errors();
#if FF_WITH_FILES
    test_watchdog_across_load();
#endif
    test_abort_request_nested();
    test_native_errors();
    test_host_exec();
#if FF_WITH_FILES
    test_load_codes();
#endif
    test_definitions();
    test_memory_limit();
#if FF_WITH_SYSTEM && FF_WITH_FILES
    test_sandbox();
#endif

    if (g_failures)
        fprintf(stderr, "%d check(s) failed.\n", g_failures);
    return g_failures ? 1 : 0;
}
