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
#include <locale.h>
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

static void nat_eval(ff_t *ff)
{
    ff_eval(ff, "100 dup");
}

static void nat_exec(ff_t *ff)
{
    ff_exec(ff, ff_dict_lookup(&ff->dict, "dup"));
}

/* A native word can run Forth itself, through ff_eval() or ff_exec(), and
   the word that called it carries on afterwards. The nested run handed
   back its own final ip, so the caller stopped right after the native
   and the rest of its body was skipped without an error. */
static void test_native_reentry(void)
{
    ff_t *ff = new_engine(10000000);

    static const ff_native_word_t words[] =
    {
        FF_NATIVE("nat-eval", nat_eval, NULL),
        FF_NATIVE("nat-exec", nat_exec, NULL),
        FF_NATIVE_END
    };
    ff_register(ff, words);

    static const int64_t want[] = { 1, 100, 100, 2, 7, 7, 3, 999 };
    CHECK(ff_eval(ff, ": t1 1 nat-eval 2 ;  : t2 7 nat-exec 3 ;"
                      "  : outer t1 t2 999 ;  outer") == FF_OK);
    CHECK(ff_depth(ff) == sizeof(want) / sizeof(want[0]));
    for (size_t i = sizeof(want) / sizeof(want[0]); i-- > 0; )
    {
        int64_t v = 0;
        CHECK(ff_pop_int(ff, &v) && v == want[i]);
    }

    ff_free(ff);
}

static void nat_square(ff_t *ff)
{
    int64_t n;
    if (ff_pop_int(ff, &n))
        ff_push_int(ff, n * n);
}

static void nat_sum_all(ff_t *ff)
{
    int64_t sum = 0, v;
    while (ff_depth(ff) > 0 && ff_pop_int(ff, &v))
        sum += v;
    ff_push_int(ff, sum);
}

static void nat_eat2(ff_t *ff)
{
    /* The internal stack API knows nothing of scope barriers. */
    ff_stack_pop(&ff->stack);
    ff_stack_pop(&ff->stack);
}

/* A native word's pushes and pops follow the rules of any word's: in a
   `{ }` scope it sees only the cells above the barrier, and running out
   of stack is its error, which stops its caller. `ff_depth` and the pops
   used to see the whole stack, so a native could eat its caller's cells,
   and a failed pop went unnoticed. */
static void test_native_stack(void)
{
    ff_t *ff = new_engine(10000000);

    static const ff_native_word_t words[] =
    {
        FF_NATIVE("square", nat_square, NULL),
        FF_NATIVE("sum-all", nat_sum_all, NULL),
        FF_NATIVE("eat2", nat_eat2, NULL),
        FF_NATIVE_END
    };
    ff_register(ff, words);

    reset_output();
    CHECK(ff_eval(ff, "clear square 42 .") == FF_ERR_STACK_UNDER);
    CHECK(strcmp(g_out, "") == 0);

    int64_t v = 0;
    CHECK(ff_eval(ff, "clear : f { ( a b -- r ) a b sum-all } ;"
                      "  1000 2000 3 4 f") == FF_OK);
    CHECK(ff_depth(ff) == 3);
    CHECK(ff_pop_int(ff, &v) && v == 7);
    CHECK(ff_pop_int(ff, &v) && v == 2000);
    CHECK(ff_pop_int(ff, &v) && v == 1000);

    /* Through the internal API a native can still pop through a barrier;
       the scope's exit reports it instead of sliding a negative count of
       cells over the stack. */
    CHECK(ff_eval(ff, ": g { ( a -- ... ) eat2 } ;  1 2 3 g")
          == FF_ERR_STACK_UNDER);

    /* From the host, between runs, a pop from an empty stack only fails. */
    CHECK(ff_eval(ff, "clear") == FF_OK);
    CHECK(!ff_pop_int(ff, &v));
    CHECK(ff_errno(ff) == FF_OK);

    ff_free(ff);
}

/* The host's calls: ff_find() gets a word for ff_exec() without internal
   headers; each call from the host starts afresh, ff_exec() as ff_eval()
   does; and the error record describes the last call only. */
static void test_host_calls(void)
{
    ff_t *ff = new_engine(100000);

    CHECK(ff_find(ff, "DUP") != NULL);
    CHECK(ff_find(ff, "no-such-word") == NULL);
    CHECK(!ff_exec(ff, NULL));
    CHECK(ff_errno(ff) == FF_ERR_BAD_PTR);

    /* The watchdog budget used to run on across ff_exec() calls, so a host
       calling a short word in a loop had every call aborted after a while;
       an abort requested between calls ended the next one. */
    CHECK(ff_eval(ff, ": spin 2000 0 do loop ;") == FF_OK);
    ff_word_t *spin = ff_find(ff, "spin");
    bool all = true;
    for (int i = 0; i < 200; ++i)
        all = ff_exec(ff, spin) && all;
    CHECK(all);
    ff_request_abort(ff);
    CHECK(ff_exec(ff, spin));

    /* A later success, or an error that `catch` handled, leaves no error
       behind. */
    CHECK(ff_eval(ff, "zork") == FF_ERR_UNDEFINED);
    CHECK(ff_errno(ff) == FF_ERR_UNDEFINED && ff_throw_code(ff) == -13);
    CHECK(ff_eval(ff, "1 drop") == FF_OK);
    CHECK(ff_errno(ff) == FF_OK && strcmp(ff_strerror(ff), "") == 0);
    CHECK(ff_eval(ff, "clear ' drop catch drop") == FF_OK);
    CHECK(ff_errno(ff) == FF_OK && ff_throw_code(ff) == 0);

    /* The program's own THROW code reaches the host. */
    CHECK(ff_eval(ff, "-1234 throw") == FF_ERR_APPLICATION);
    CHECK(ff_throw_code(ff) == -1234);

    /* An error the host raises itself, with nothing running, is recorded
       but leaves nothing to unwind: it used to make every later call
       return at once, running nothing. */
    ff_tracef(ff, FF_SEV_ERROR | FF_ERR_BAD_PTR, "Host error.");
    CHECK(ff_errno(ff) == FF_ERR_BAD_PTR);
    reset_output();
    CHECK(ff_eval(ff, "1 2 + .") == FF_OK);
    CHECK(strcmp(g_out, "3") == 0);

    ff_free(ff);
}

/* Reals read and print with '.' whatever the host's locale. Under one
   with a decimal comma, `1.5` was an undefined word, `2,5` a real, and
   `f.` printed a comma. Skipped where no such locale is installed. */
static void test_real_locale(void)
{
    static const char *const comma[] =
        { "ru_RU.UTF-8", "ru_RU.utf8", "de_DE.UTF-8", "de_DE.utf8",
          "fr_FR.UTF-8", "fr_FR.utf8", NULL };
    const char *set = NULL;
    for (int i = 0; comma[i] && !set; ++i)
        set = setlocale(LC_NUMERIC, comma[i]);
    if (!set || strcmp(localeconv()->decimal_point, ".") == 0)
    {
        setlocale(LC_NUMERIC, "C");
        fprintf(stderr, "test_real_locale: no decimal-comma locale, skipped\n");
        return;
    }

    ff_t *ff = new_engine(10000000);
    reset_output();
    CHECK(ff_eval(ff, "1.5 f. 32 emit 2.25e1 f. 32 emit 1.5 2.0 f* f.") == FF_OK);
    CHECK(strcmp(g_out, "1.5 22.5 3") == 0);
    CHECK(ff_eval(ff, "2,5") == FF_ERR_UNDEFINED);
    reset_output();
    CHECK(ff_eval(ff, ": r 0.25 ;  see r") == FF_OK);
    CHECK(strstr(g_out, "0.25") != NULL);
    ff_free(ff);

    setlocale(LC_NUMERIC, "C");
}

/* The dictionary's indexes match its words exactly: every heap's region
   is in the arena's index — at its address, to its capacity — the index
   is sorted without overlaps, and by_addr holds every word in order. */
static bool indexes_match(const ff_t *ff)
{
    const ff_dict_t  *d = &ff->dict;
    const ff_arena_t *a = &d->arena;
    size_t with_data = 0;
    for (size_t i = 0; i < d->count; ++i)
    {
        const ff_heap_t *h = &d->words[i]->heap;
        if (!ff_dict_contains(d, d->words[i]))
            return false;
        if (!h->data)
            continue;
        ++with_data;
        const ff_region_t *r = ff_dict_region_at(d, h->data);
        if (!r || r->lo != (uintptr_t)h->data || r->owner != h
                || r->hi != (uintptr_t)h->data + h->capacity * sizeof(ff_int_t))
            return false;
    }
    for (size_t i = 1; i < a->n_regions; ++i)
        if (a->regions[i - 1].hi > a->regions[i].lo)
            return false;
    for (size_t i = 1; i < d->count; ++i)
        if ((uintptr_t)d->by_addr[i - 1] >= (uintptr_t)d->by_addr[i])
            return false;
    return a->n_regions == with_data;
}

/* The arena grows heaps in place when it can and indexes their regions as
   they change; words are removed singly and wholesale. A random mix of
   the operations that move things — data words filled past a slab,
   definitions that go on growing after a word made between `[` and `]`,
   failed definitions, forgets — must leave the indexes matching the
   dictionary, and every heap readable where the index says it is. */
static void test_dict_indexes(void)
{
    ff_t *ff = new_engine(1000000000);
    uint32_t seed = 12345;
    char src[512];
    int made = 0;
    bool ok = ff_eval(ff, ": fill 0 do i , loop ;") == FF_OK;
    size_t most = 0, peak = 0;

    for (int step = 0; step < 3000 && ok; ++step)
    {
        seed = seed * 1103515245u + 12345u;
        uint32_t r = seed >> 8;
        switch (r % 6)
        {
            case 0:
                snprintf(src, sizeof src, "variable v%d  %u v%d !",
                         made, r, made);
                ++made;
                break;
            case 1:
                snprintf(src, sizeof src, "create c%d  %u fill",
                         made, r % 3000);
                ++made;
                break;
            case 2:
                snprintf(src, sizeof src,
                         ": d%d [ create x%d 1 , ] 1 2 3 4 5 6 7 8 9"
                         " + + + + + + + + drop ;", made, made);
                ++made;
                break;
            case 3:
                snprintf(src, sizeof src, ": bad%d 1 2 zork ;", made);
                break;
            case 4:
                if (made == 0)
                    continue;
                snprintf(src, sizeof src, "\"forget v%u\" evaluate drop",
                         r % (unsigned)made);
                break;
            default:
                snprintf(src, sizeof src, "create big%d  %u fill",
                         made, 5000 + r % 20000);
                ++made;
                break;
        }
        (void)ff_eval(ff, src);
        ok = indexes_match(ff);
        /* The newest data word reads back from where the index says. */
        const ff_word_t *top = ff_dict_top(&ff->dict);
        if (ok && top && top->heap.size && ff_word_holds_data(top))
            ok = ff_addr_valid(ff, &top->heap.data[top->heap.size - 1],
                               sizeof(ff_int_t));
        if (top && top->heap.size > most)
            most = top->heap.size;
        if (ff->dict.count > peak)
            peak = ff->dict.count;
    }
    CHECK(ok);
    /* It did fill words past a slab, and grow a sizeable dictionary
       before `forget` cut it back. */
    CHECK(most >= 5000 && peak > 200);

    ff_free(ff);
}

static int g_non_unique;

static int note_trace(void *ctx, ff_error_t e, const char *fmt, va_list args)
{
    (void)ctx;
    (void)fmt;
    (void)args;
    if (FF_ERR_CODE(e) == FF_ERR_NON_UNIQUE)
        ++g_non_unique;
    return 0;
}

static void nat_nop(ff_t *ff)
{
    (void)ff;
}

/* ff_register() takes only names the interpreter can read back as that
   one word, warns of shadowing as `:` does, and `forget` leaves what it
   registers. It took "", "two words" or "42" — words no program could
   call — shadowed built-ins without a word, and a script's `forget`
   removed the host's words. */
static void test_register_checks(void)
{
    ff_platform_t p = { .vprintf = capture_vprintf, .vtracef = note_trace };
    ff_t *ff = ff_new(&p);

    static const char *const bad[] =
        { "", "two words", "42", "1.5", "\"s\"", "(", "\\", NULL };
    for (int i = 0; bad[i]; ++i)
    {
        ff_native_word_t t[] = { FF_NATIVE(bad[i], nat_nop, NULL), FF_NATIVE_END };
        CHECK(ff_register(ff, t) == FF_ERR_MALFORMED);
    }
    ff_native_word_t no_fn[] = { FF_NATIVE("no-fn", NULL, NULL), FF_NATIVE_END };
    CHECK(ff_register(ff, no_fn) == FF_ERR_MALFORMED);
    CHECK(ff_find(ff, "no-fn") == NULL);

    g_non_unique = 0;
    static const ff_native_word_t good[] =
    {
        FF_NATIVE("host-word", nat_nop, NULL),
        FF_NATIVE("swap", nat_nop, NULL),
        FF_NATIVE_END
    };
    CHECK(ff_register(ff, good) == FF_OK);
    CHECK(g_non_unique == 1);

    CHECK(ff_eval(ff, "forget host-word") == FF_ERR_FORGET_PROT);
    CHECK(ff_find(ff, "host-word") != NULL);
    CHECK(ff_eval(ff, ": mine ;  forget mine") == FF_OK);

    ff_free(ff);
}

static void nat_ctx(ff_t *ff)
{
    ff_push_int(ff, *(const int *)ff_context(ff));
}

static void nat_hello(ff_t *ff)
{
    ff_push_str(ff, "hello, world", 5);
}

static void nat_slen(ff_t *ff)
{
    const char *s;
    if (ff_pop_str(ff, &s))
        ff_push_int(ff, (int64_t)strlen(s));
}

static void nat_nope(ff_t *ff)
{
    ff_throwf(ff, -5000, "Host said no (%d).", 7);
}

/* What a native word needs besides numbers: the host's own data, strings
   both ways, and throwing a code of the program's own. */
static void test_native_facilities(void)
{
    int ctx = 99;
    ff_platform_t p = { .context = &ctx, .vprintf = capture_vprintf };
    ff_t *ff = ff_new(&p);
    static const ff_native_word_t words[] =
    {
        FF_NATIVE("ctx", nat_ctx, NULL),
        FF_NATIVE("hello", nat_hello, NULL),
        FF_NATIVE("slen", nat_slen, NULL),
        FF_NATIVE("nope", nat_nope, NULL),
        FF_NATIVE_END
    };
    CHECK(ff_register(ff, words) == FF_OK);
    CHECK(ff_context(ff) == &ctx);

    reset_output();
    CHECK(ff_eval(ff, "ctx . 32 emit hello type 32 emit \"abcd\" slen .") == FF_OK);
    CHECK(strcmp(g_out, "99 hello 4") == 0);

    const char *s = NULL;
    CHECK(ff_push_str(ff, "from the host", 13));
    CHECK(ff_eval(ff, "dup slen swap") == FF_OK);
    CHECK(ff_pop_str(ff, &s) && strcmp(s, "from the host") == 0);
    int64_t n = 0;
    CHECK(ff_pop_int(ff, &n) && n == 13);

    reset_output();
    CHECK(ff_eval(ff, "' nope catch .") == FF_OK);
    CHECK(strcmp(g_out, "-5000") == 0);
    CHECK(ff_eval(ff, "nope") == FF_ERR_APPLICATION);
    CHECK(ff_throw_code(ff) == -5000);
    CHECK(strcmp(ff_strerror(ff), "Host said no (7).") == 0);

    ff_free(ff);
}

/* The transient string arena only grew, until the memory limit made
   every allocation fail; ff_release_strings() gives it back. */
static void test_release_strings(void)
{
    ff_t *ff = new_engine(100000000);
    CHECK(ff_eval(ff, "\"kept\" constant kept") == FF_OK);
    size_t before = ff->dict.mem.used;
    for (int i = 0; i < 2000; ++i)
        (void)ff_eval(ff, "\"a string literal typed at the prompt\" drop");
    CHECK(ff->dict.mem.used > before);
    ff_release_strings(ff);
    CHECK(ff->dict.mem.used < before);
#if FF_SAFE_MEM
    /* A string the program kept is gone, and refused. */
    CHECK(ff_eval(ff, "kept type") == FF_ERR_BAD_PTR);
#endif
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
    test_native_reentry();
    test_native_stack();
    test_host_calls();
    test_dict_indexes();
    test_real_locale();
    test_register_checks();
    test_native_facilities();
    test_release_strings();
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
