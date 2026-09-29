/**
 * @file ff.c
 * @brief Engine top-level: lifecycle, source evaluator, and the inner
 *        interpreter that walks compiled bytecode via a single switch.
 *
 * Built-in word bodies live in per-category `words/ff_words_*_p.h`
 * files that are #include'd inside the switch in @ref ff_exec, so the
 * generated code is one indirect jump per opcode — matching the speed
 * of computed-goto dispatch on modern GCC/Clang while still building
 * cleanly under MSVC.
 *
 * The data-stack TOS is cached in a local register inside ff_exec; see
 * the @c _FF_PUSH / @c _FF_DROP / @c _FF_NOS / @c _FF_SAT macros and the
 * @c _FF_SYNC_TOS / @c _FF_LOAD_TOS hooks woven into @c _FF_SYNC and
 * @c _FF_RESTORE for the contract.
 */

#include "ff_p.h"
#include "ff_opcode_meta_p.h"
#include "ff_real_p.h"

#include <fort/fort.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>


/**
 * @def FF_UNREACHABLE
 * @brief Compiler hint marking a branch that the optimizer can prove
 *        unreachable.
 *
 * Used at the @c default arm of the inner-interpreter switch so the
 * compiler can elide bounds checks on the dispatch table.
 */
#if defined(__GNUC__) || defined(__clang__)
#define FF_UNREACHABLE() __builtin_unreachable()
#elif defined(_MSC_VER)
#define FF_UNREACHABLE() __assume(0)
#else
#define FF_UNREACHABLE() ((void)0)
#endif

/**
 * @def FF_WD_BATCH
 * @brief How many watchdog ticks (back-branches / word calls) elapse
 *        between syncs of the local counter to ff->opcodes_run and
 *        the atomic abort-flag check.
 *
 * Higher values shave dispatch overhead at the cost of async-abort
 * latency: an ff_request_abort takes up to FF_WD_BATCH back-branches
 * to be observed. At 3-4 ns per back-branch on a Ryzen-class core,
 * 256 ticks = ~1 µs latency.
 */
#define FF_WD_BATCH 256


static void ff_csig_clear(ff_t *ff);
static bool ff_idle(const ff_t *ff);
static char *ff_pad_intern(ff_t *ff, const char *s, size_t len);
static void ff_pad_release(ff_t *ff);
static bool ff_mem_check(ff_t *ff);
static void ff_raisev(ff_t *ff, ff_int_t code, ff_error_t e,
                      const char *fmt, va_list args);
static ff_error_t ff_error_from_throw(ff_int_t n);


// Public

/** @copydoc ff_new */
ff_t *ff_new(const ff_platform_t *p)
{
    assert(p);

    ff_t *ff = (ff_t *)calloc(1, sizeof(ff_t));
    if (!ff)
        return NULL;

    ff->platform = *p;

    ff->base = FF_BASE_DEC;

    /* Per-engine dict delegates built-in lookups to the process-wide
       singleton — initialised lazily here on the first ff_new call (see
       ff_builtins_default() about threads). Out of memory in either, the
       engine isn't made. */
    const ff_builtins_t *builtins = ff_builtins_default();
    if (!builtins || !ff_dict_init(&ff->dict, builtins))
    {
        ff_dict_destroy(&ff->dict);
        free(ff);
        return NULL;
    }
    ff->dict.mem.limit = p->mem_limit;
    ff_stack_init(&ff->stack);
    ff_stack_init(&ff->r_stack);
    ff_bt_stack_init(&ff->bt_stack);
    ff_tokenizer_init(&ff->tokenizer);

    return ff;
}

/** @copydoc ff_free */
void ff_free(ff_t *ff)
{
    if (!ff)
        return;

    /* Streams the program opened and never closed. */
    for (int i = 0; i < FF_OPEN_FILES_MAX; ++i)
        if (ff->files[i])
            fclose(ff->files[i]);

    /* A definition left open when the input ended keeps the input names
       of its open `{` scopes. */
    ff_csig_clear(ff);

    ff_tokenizer_destroy(&ff->tokenizer);
    ff_bt_stack_destroy(&ff->bt_stack);
    ff_stack_destroy(&ff->r_stack);
    ff_stack_destroy(&ff->stack);
    ff_dict_destroy(&ff->dict);
    for (ff_pad_slab_t *sl = ff->pad; sl; )
    {
        ff_pad_slab_t *next = sl->next;
        free(sl);
        sl = next;
    }

    free(ff);
}

/** @copydoc ff_version */
const char *ff_version(void)
{
    return FF_VERSION;
}

/** @copydoc ff_warmup */
bool ff_warmup(void)
{
    return ff_builtins_default() != NULL;
}

/**
 * @param name Proposed word name.
 * @return true if the interpreter would read @p name as that one word —
 *         not empty, no spaces, not a number, a string or a comment.
 */
static bool ff_name_readable(const char *name)
{
    ff_tokenizer_t t;
    ff_tokenizer_init(&t);
    int pos = 0;
    return ff_tokenizer_next(&t, name, &pos) == FF_TOKEN_WORD
        && !t.truncated
        && strcmp(t.token, name) == 0
        && name[pos] == '\0';
}

/** @copydoc ff_register */
ff_error_t ff_register(ff_t *ff, const ff_native_word_t *words)
{
    for (const ff_native_word_t *w = words; w && w->name; ++w)
    {
        if (!ff_name_readable(w->name) || !w->fn)
            return FF_ERR_CODE(ff_tracef(ff, FF_SEV_ERROR | FF_ERR_MALFORMED,
                                         "Can't register '%s': %s.", w->name,
                                         w->fn ? "not a name the interpreter can read"
                                               : "no function"));
        if (ff_dict_lookup(&ff->dict, w->name))
            ff_tracef(ff, FF_SEV_WARNING | FF_ERR_NON_UNIQUE,
                      "'%s' isn't unique.", w->name);
        ff_word_t *nw = ff_dict_append(&ff->dict,
                                       w->immediate
                                           ? ff_im_word_new(w->name, w->fn, FF_OP_NONE, w->manual)
                                           : ff_word_new(w->name, w->fn, FF_OP_NONE, w->manual));
        if (!nw)
            return FF_ERR_OOM;
        nw->flags |= FF_WORD_HOST;
    }
    return FF_OK;
}

/** @copydoc ff_context */
void *ff_context(const ff_t *ff)
{
    return ff->platform.context;
}

/** @copydoc ff_find */
ff_word_t *ff_find(ff_t *ff, const char *name)
{
    return name ? ff_dict_lookup(&ff->dict, name) : NULL;
}

/** @copydoc ff_depth */
size_t ff_depth(const ff_t *ff)
{
    /* Inside a `{ }` scope only the cells above its barrier belong to the
       running code, as for the `depth` word. */
    return ff->stack.top - ff->stack.floor;
}

/**
 * A push or pop through the public stack API that didn't fit. Called by
 * the host between runs, it just fails. Inside a running word — a native
 * word's own push or pop — it is that word's stack error, raised as any
 * word's would be, so the word that called it stops too: a native that
 * only checked the return value used to carry on as if nothing happened.
 *
 * @param ff   Engine.
 * @param over true for an overflow, false for an underflow.
 * @return false.
 */
static bool ff_api_stack_fail(ff_t *ff, bool over)
{
    if (!ff_idle(ff))
    {
        if (over)
            ff_tracef(ff, FF_SEV_ERROR | FF_ERR_STACK_OVER,
                      "Stack overflow: 1 item(s) would not fit.");
        else
            ff_tracef(ff, FF_SEV_ERROR | FF_ERR_STACK_UNDER,
                      "Stack underflow: 1 item(s) expected.");
    }
    return false;
}

/** @copydoc ff_push_int */
bool ff_push_int(ff_t *ff, int64_t v)
{
    if (ff->stack.top >= FF_STACK_SIZE)
        return ff_api_stack_fail(ff, true);
    ff_stack_push(&ff->stack, (ff_int_t)v);
    return true;
}

/** @copydoc ff_pop_int */
bool ff_pop_int(ff_t *ff, int64_t *out)
{
    if (ff->stack.top <= ff->stack.floor)
        return ff_api_stack_fail(ff, false);
    ff_int_t v = ff_stack_pop(&ff->stack);
    if (out)
        *out = (int64_t)v;
    return true;
}

/** @copydoc ff_push_real */
bool ff_push_real(ff_t *ff, double v)
{
    if (ff->stack.top >= FF_STACK_SIZE)
        return ff_api_stack_fail(ff, true);
    ff_stack_push_real(&ff->stack, (ff_real_t)v);
    return true;
}

/** @copydoc ff_push_str */
bool ff_push_str(ff_t *ff, const char *s, size_t len)
{
    if (ff->stack.top >= FF_STACK_SIZE)
        return ff_api_stack_fail(ff, true);
    char *copy = ff_pad_intern(ff, s ? s : "", s ? len : 0);
    if (!copy)
    {
        /* Over the memory limit, or out of memory: the host gets the
           error recorded; a running word, raised. */
        ff_mem_check(ff);
        return false;
    }
    ff_stack_push_ptr(&ff->stack, copy);
    return true;
}

/** @copydoc ff_pop_str */
bool ff_pop_str(ff_t *ff, const char **out)
{
    if (ff->stack.top <= ff->stack.floor)
        return ff_api_stack_fail(ff, false);
    const char *s = (const char *)(intptr_t)ff->stack.data[ff->stack.top - 1];
#if FF_SAFE_MEM
    if (!ff_str_valid(ff, s))
    {
        if (!ff_idle(ff))
            ff_tracef(ff, FF_SEV_ERROR | FF_ERR_BAD_PTR, "Bad string address.");
        return false;
    }
#endif
    --ff->stack.top;
    if (out)
        *out = s;
    return true;
}

/** @copydoc ff_throwf */
void ff_throwf(ff_t *ff, int64_t code, const char *fmt, ...)
{
    if (code == 0)
        return;     /* `0 throw` does nothing either */
    va_list args;
    va_start(args, fmt);
    ff_raisev(ff, (ff_int_t)code,
              FF_SEV_ERROR | ff_error_from_throw((ff_int_t)code), fmt, args);
    va_end(args);
}

/** @copydoc ff_release_strings */
void ff_release_strings(ff_t *ff)
{
    if (ff_idle(ff))
        ff_pad_release(ff);
}

/** @copydoc ff_pop_real */
bool ff_pop_real(ff_t *ff, double *out)
{
    if (ff->stack.top <= ff->stack.floor)
        return ff_api_stack_fail(ff, false);
    ff_int_t bits = ff_stack_pop(&ff->stack);
    ff_real_t r;
    memcpy(&r, &bits, sizeof(r));
    if (out)
        *out = (double)r;
    return true;
}

/**
 * Copy @p len bytes of @p s into the transient string arena as a
 * NUL-terminated C string and return a stable pointer to it.
 *
 * The arena grows on demand and is reset only by ff_abort, so the
 * returned pointer stays valid for the engine's lifetime (or until the
 * next abort). Backs interpret-time string literals as well as
 * `parse-word` / `parse`.
 *
 * @param ff  Engine.
 * @param s   Source bytes (need not be NUL-terminated).
 * @param len Number of bytes to copy.
 * @return Interned C string, or NULL on allocation failure.
 */
static char *ff_pad_intern(ff_t *ff, const char *s, size_t len)
{
    size_t need = len + 1;
    ff_pad_slab_t *sl = ff->pad;

    /* Allocate a fresh slab when the head can't fit the request. Existing
       slabs are never touched, so every pointer previously returned stays
       valid. A request larger than the default slab gets a dedicated slab
       sized to fit. */
    if (!sl || sl->used + need > sl->size)
    {
        /* A slab counts against the memory limit; near the limit it is
           cut down to what's left, so a string that fits still fits. */
        ff_mem_t *m = &ff->dict.mem;
        size_t cap = need > (size_t)FF_PAD_INIT_SIZE
                         ? need : (size_t)FF_PAD_INIT_SIZE;
        size_t room = ff_mem_room(m);
        if (room < sizeof(ff_pad_slab_t) + need)
        {
            ff_mem_refuse(m, need, false);
            return NULL;
        }
        if (cap > room - sizeof(ff_pad_slab_t))
            cap = room - sizeof(ff_pad_slab_t);
        ff_pad_slab_t *ns = (ff_pad_slab_t *)malloc(sizeof(*ns) + cap);
        if (!ns)
        {
            ff_mem_refuse(m, need, true);
            return NULL;
        }
        ff_mem_charge(m, sizeof(*ns) + cap);
        ns->next = ff->pad;
        ns->used = 0;
        ns->size = cap;
        ff->pad = ns;
        sl = ns;
    }

    char *dst = sl->data + sl->used;
    if (len)
        memcpy(dst, s, len);
    dst[len] = '\0';
    sl->used += need;
    return dst;
}

/**
 * Free the transient string arena: every string in it goes, and its
 * memory is handed back to the account.
 *
 * @param ff Engine.
 */
static void ff_pad_release(ff_t *ff)
{
    for (ff_pad_slab_t *sl = ff->pad; sl; )
    {
        ff_pad_slab_t *next = sl->next;
        ff_mem_release(&ff->dict.mem, sizeof(*sl) + sl->size);
        free(sl);
        sl = next;
    }
    ff->pad = NULL;
}

/**
 * @param ff Engine.
 * @return true if nothing is running: no evaluation, no word. A call
 *         into the engine now comes from the host itself.
 */
static bool ff_idle(const ff_t *ff)
{
    return ff->eval_depth == 0 && ff->exec_depth == 0;
}

/**
 * Forget the last error: ff_errno() reports FF_OK again.
 * @param ff Engine.
 */
static void ff_error_clear(ff_t *ff)
{
    ff->error        = FF_OK;
    ff->error_msg[0] = '\0';
    ff->error_line   = 0;
    ff->error_pos    = 0;
}

/**
 * Start a call from the host into an idle engine — ff_eval(), ff_load()
 * or ff_exec() with nothing running.
 *
 * The watchdog state belongs to the outermost call, and so does the
 * error record: ff_errno() describes what ended the last call, not an
 * earlier one. Resetting the watchdog on a nested entry (`evaluate`,
 * `load`, or a native word evaluating source) would restart the opcode
 * budget and drop a pending ff_request_abort() on every pass through a
 * loop around them, so untrusted code could never be stopped; not
 * resetting it on a host's ff_exec() made the budget of every earlier
 * call count against the next one.
 *
 * @param ff Engine.
 */
static void ff_host_enter(ff_t *ff)
{
    FF_ABORT_CLEAR(&ff->abort_requested);
    ff->opcodes_run      = 0;
    ff->next_watchdog_at = ff->platform.watchdog_interval
                               ? ff->platform.watchdog_interval
                               : 65536;
    ff->state &= ~FF_STATE_THROWN;
    ff_error_clear(ff);
}

/**
 * End a call from the host (see ff_host_enter()). One that succeeded
 * leaves no error record behind, even if `catch` or `evaluate` handled
 * errors on the way.
 *
 * @param ff Engine.
 * @param ec What the call returns.
 * @return @p ec.
 */
static ff_error_t ff_host_leave(ff_t *ff, ff_error_t ec)
{
    if (ec == FF_OK)
        ff_error_clear(ff);
    return ec;
}

/**
 * Enter an evaluation (ff_eval or ff_load).
 * @param ff Engine.
 */
static void ff_eval_enter(ff_t *ff)
{
    ++ff->eval_depth;
}

/**
 * Leave an evaluation entered with ff_eval_enter().
 * @param ff Engine.
 */
static void ff_eval_leave(ff_t *ff)
{
    --ff->eval_depth;
}

/**
 * Check there is room for one outer-interpreter push. The evaluator
 * pushes literals itself, outside ff_exec's dispatch-context checks, so
 * without this a long enough line of literals overruns the stack array.
 *
 * @param ff Engine.
 * @param ec Set to the raised error on failure.
 * @return false if the data stack is full.
 */
static bool ff_eval_room(ff_t *ff, ff_error_t *ec)
{
    if (ff_unlikely(ff->stack.top >= FF_STACK_SIZE))
    {
        *ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_STACK_OVER,
                        "Stack overflow: %d item(s) would not fit.", 1);
        return false;
    }
    return true;
}


/* ===================================================================
 * Exceptions.
 *
 * Every error is an exception. Raising one records its THROW code (ANS
 * numbering, see ff_throw_p.h) and, for an error, the message and source
 * location, then sets FF_STATE_THROWN. Execution stops at once: the
 * dispatch loop leaves ff_exec, and each C frame that re-entered the
 * interpreter checks the flag on the way out — a native word's
 * FF_OP_CALL, `catch`, `evaluate`, `load` — and keeps unwinding unless it
 * settles the exception there.
 * =================================================================== */

static void ff_error_locate(const ff_t *ff, int *line, int *pos);

/**
 * THROW code for an engine error: its ANS Forth code (Table 9.1) where
 * one exists, so `catch` sees the standard values, and `-(256 + code)`
 * from the system-defined range otherwise.
 *
 * @param code Bare FF_ERR_* code.
 * @return THROW code.
 */
static ff_int_t ff_throw_from_error(ff_error_t code)
{
    switch (code)
    {
        case FF_ERR_STACK_OVER:   return FF_THROW_STACK_OVER;
        case FF_ERR_STACK_UNDER:  return FF_THROW_STACK_UNDER;
        case FF_ERR_RSTACK_OVER:  return FF_THROW_RSTACK_OVER;
        case FF_ERR_RSTACK_UNDER: return FF_THROW_RSTACK_UNDER;
        case FF_ERR_HEAP_OVER:    return FF_THROW_DICT_OVER;
        case FF_ERR_BAD_PTR:      return FF_THROW_BAD_ADDRESS;
        case FF_ERR_DIV_ZERO:     return FF_THROW_DIV_ZERO;
        case FF_ERR_UNDEFINED:    return FF_THROW_UNDEFINED;
        case FF_ERR_NOT_IN_DEF:   return FF_THROW_COMPILE_ONLY;
        case FF_ERR_FORGET_PROT:  return FF_THROW_BAD_FORGET;
        case FF_ERR_UNSUPPORTED:  return FF_THROW_UNSUPPORTED;
        case FF_ERR_RSTACK_IMBAL: return FF_THROW_RSTACK_IMBAL;
        case FF_ERR_ABORTED:      return FF_THROW_INTERRUPT;
        case FF_ERR_FILE_IO:      return FF_THROW_FILE_IO;
        case FF_ERR_OOM:          return FF_THROW_ALLOCATE;
        default:                  return FF_THROW_SYSTEM_BASE - (ff_int_t)code;
    }
}

/**
 * FF_ERR_* an API call reports for an uncaught THROW code — the inverse
 * of ff_throw_from_error(), with ABORT / ABORT" reported as
 * FF_ERR_ABORTED and any code the engine doesn't define as
 * FF_ERR_APPLICATION.
 *
 * @param n THROW code.
 * @return Bare FF_ERR_* code.
 */
static ff_error_t ff_error_from_throw(ff_int_t n)
{
    switch (n)
    {
        case FF_THROW_ABORT:
        case FF_THROW_ABORTQ:
        case FF_THROW_INTERRUPT:     return FF_ERR_ABORTED;
        case FF_THROW_STACK_OVER:    return FF_ERR_STACK_OVER;
        case FF_THROW_STACK_UNDER:   return FF_ERR_STACK_UNDER;
        case FF_THROW_RSTACK_OVER:   return FF_ERR_RSTACK_OVER;
        case FF_THROW_RSTACK_UNDER:  return FF_ERR_RSTACK_UNDER;
        case FF_THROW_DICT_OVER:     return FF_ERR_HEAP_OVER;
        case FF_THROW_BAD_ADDRESS:   return FF_ERR_BAD_PTR;
        case FF_THROW_DIV_ZERO:      return FF_ERR_DIV_ZERO;
        case FF_THROW_UNDEFINED:     return FF_ERR_UNDEFINED;
        case FF_THROW_COMPILE_ONLY:  return FF_ERR_NOT_IN_DEF;
        case FF_THROW_BAD_FORGET:    return FF_ERR_FORGET_PROT;
        case FF_THROW_UNSUPPORTED:   return FF_ERR_UNSUPPORTED;
        case FF_THROW_CS_MISMATCH:
        case FF_THROW_BAD_ARG:
        case FF_THROW_NESTING:       return FF_ERR_MALFORMED;
        case FF_THROW_RSTACK_IMBAL:  return FF_ERR_RSTACK_IMBAL;
        case FF_THROW_FILE_IO:       return FF_ERR_FILE_IO;
        case FF_THROW_ALLOCATE:      return FF_ERR_OOM;
        default:
            if (n < FF_THROW_SYSTEM_BASE
                    && n >= FF_THROW_SYSTEM_BASE - FF_ERR_APPLICATION)
                return (ff_error_t)(FF_THROW_SYSTEM_BASE - n);
            return FF_ERR_APPLICATION;
    }
}

/**
 * @param code THROW code.
 * @return true for the exceptions `catch` must not stop: the host's abort
 *         (watchdog, ff_request_abort) — or untrusted code could swallow
 *         it and run on — and QUIT, which returns to the host by design.
 */
static bool ff_throw_is_fatal(ff_int_t code)
{
    return code == FF_THROW_INTERRUPT || code == FF_THROW_QUIT;
}

/**
 * Put an exception carrying THROW code @p code in flight.
 *
 * While another exception is already in flight, that one stands — unless
 * the new one is uncatchable and the old one isn't, so nothing raised
 * while unwinding can hide a host abort.
 *
 * @param ff   Engine.
 * @param code THROW code.
 * @return false if an earlier exception stands.
 */
static bool ff_exc_begin(ff_t *ff, ff_int_t code)
{
    if ((ff->state & FF_STATE_THROWN)
            && (ff_throw_is_fatal(ff->throw_code) || !ff_throw_is_fatal(code)))
        return false;

    ff->state |= FF_STATE_THROWN;
    ff->throw_code = code;
    return true;
}

/**
 * Raise an exception carrying THROW code @p code, recording @p e
 * (severity | FF_ERR_*), the message and the source location for the
 * host.
 *
 * @param ff   Engine.
 * @param code THROW code.
 * @param e    Severity and FF_ERR_* code for the host.
 * @param fmt  printf format of the message.
 * @param args Format arguments.
 */
static void ff_raisev(ff_t *ff, ff_int_t code, ff_error_t e,
                      const char *fmt, va_list args)
{
    if (!ff_exc_begin(ff, code))
        return;

    ff->error = e;
    ff_error_locate(ff, &ff->error_line, &ff->error_pos);
    vsnprintf(ff->error_msg, sizeof(ff->error_msg), fmt, args);

    /* Raised with nothing running — by the host itself, through
       ff_tracef() — there is nothing to unwind: the error is recorded
       for ff_errno(), and the engine stays ready. Left in flight, it
       made every later call return at once without running anything. */
    if (ff_idle(ff))
        ff->state &= ~FF_STATE_THROWN;
}

/**
 * printf-style ff_raisev().
 * @param ff   Engine.
 * @param code THROW code.
 * @param e    Severity and FF_ERR_* code for the host.
 * @param fmt  printf format of the message.
 * @param ...  Format arguments.
 */
static void ff_raise(ff_t *ff, ff_int_t code, ff_error_t e,
                     const char *fmt, ...) FF_PRINTF_FMT(4, 5);

static void ff_raise(ff_t *ff, ff_int_t code, ff_error_t e,
                     const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ff_raisev(ff, code, e, fmt, args);
    va_end(args);
}

/**
 * THROW @p code on behalf of Forth code (`throw`, `quit`). QUIT records no
 * error; any other code is described as an uncaught exception, which is
 * the only way the host ever sees it.
 *
 * @param ff   Engine.
 * @param code Non-zero THROW code.
 */
static void ff_throw(ff_t *ff, ff_int_t code)
{
    if (code == FF_THROW_QUIT)
        (void)ff_exc_begin(ff, code);
    else
        ff_raise(ff, code, FF_SEV_ERROR | ff_error_from_throw(code),
                 "Uncaught exception %" FF_PRIdCELL ".", code);
}

/**
 * Raise the allocation refusal the dictionary's account recorded, if
 * any: -8 when the memory limit (or a size too large to represent)
 * refused it, -59 when the host allocator did. Called wherever code
 * that allocated would otherwise carry on as if it had succeeded.
 *
 * @param ff Engine.
 * @return true if an exception was raised.
 */
static bool ff_mem_check(ff_t *ff)
{
    ff_mem_t *m = &ff->dict.mem;
    if (ff_likely(!m->failed))
        return false;
    m->failed = false;
    if (m->oom)
        ff_raise(ff, FF_THROW_ALLOCATE, FF_SEV_ERROR | FF_ERR_OOM,
                 "Out of memory allocating %zu bytes.", m->refused);
    else if (m->limit && m->refused != SIZE_MAX)
        ff_raise(ff, FF_THROW_DICT_OVER, FF_SEV_ERROR | FF_ERR_HEAP_OVER,
                 "Memory limit reached: %zu more bytes wanted, %zu of %zu in use.",
                 m->refused, m->used, m->limit);
    else
        ff_raise(ff, FF_THROW_DICT_OVER, FF_SEV_ERROR | FF_ERR_HEAP_OVER,
                 "Allocation too large.");
    return true;
}

/* ===================================================================
 * Streams opened for Forth code.
 * =================================================================== */

/**
 * Open @p path for `fopen`, `load` or ff_load(), through the host's
 * ff_platform::open_file when it set one.
 *
 * @param ff   Engine.
 * @param path File name.
 * @param mode fopen() mode.
 * @return The stream, or NULL with errno set.
 */
static FILE *ff_open_file(ff_t *ff, const char *path, const char *mode)
{
    if (ff->platform.open_file)
        return ff->platform.open_file(ff->platform.context, path, mode);
    return fopen(path, mode);
}

#if FF_WITH_FILES
/**
 * @param ff Engine.
 * @param f  Stream, or NULL to look for a free slot.
 * @return Index of @p f in the engine's table of open streams, or -1.
 */
static int ff_file_slot(const ff_t *ff, const FILE *f)
{
    for (int i = 0; i < FF_OPEN_FILES_MAX; ++i)
        if (ff->files[i] == f)
            return i;
    return -1;
}

#if FF_SAFE_MEM
/**
 * @param ff       Engine.
 * @param f        Candidate stream from the data stack.
 * @param std_too  Accept stdin / stdout / stderr as well.
 * @return Whether @p f is a stream the program opened (or a standard one).
 */
static bool ff_file_valid(const ff_t *ff, const FILE *f, bool std_too)
{
    if (!f)
        return false;
    if (std_too && (f == stdin || f == stdout || f == stderr))
        return true;
    return ff_file_slot(ff, f) >= 0;
}
#endif
#endif

/* ===================================================================
 * Definitions and control structures.
 *
 * A colon definition compiles into ff->compiling, from `:` to `;`. An
 * exception that ends the evaluation the definition was begun in
 * abandons it: the word, which could only ever run the part of its body
 * compiled so far, is removed again. Each control structure inside it has
 * a record on ff->cf (see ff_cf_p.h) that its closer is checked against.
 * None of this is on the dispatch hot path.
 * =================================================================== */

/** @copydoc ff_parse */
const char *ff_parse(ff_t *ff, const char *word, ff_token_t kind)
{
    ff_tokenizer_t *t = &ff->tokenizer;
    ff_token_t tok = ff->input
                         ? ff_tokenizer_next(t, ff->input, &ff->input_pos)
                         : FF_TOKEN_NULL;
    const char *what = kind == FF_TOKEN_STRING ? "a string literal" : "a name";

    if (t->truncated)
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_MALFORMED,
                  "Token longer than %d bytes.", FF_TOKEN_SIZE - 1);
    else if (t->bad_escape)
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_MALFORMED,
                  "Invalid escape sequence in string literal.");
    else if (tok == FF_TOKEN_NULL && (t->state & FF_TOK_STATE_STRING))
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_RUN_STRING,
                  "Unterminated string literal.");
    else if (tok == FF_TOKEN_NULL)
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_MISSING,
                  "'%s' needs %s after it on the same line.", word, what);
    else if (tok != kind)
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_MISSING,
                  "'%s' needs %s, got '%s'.", word, what, t->token);
    else
        return t->token;
    return NULL;
}

/** @copydoc ff_parse_word */
ff_word_t *ff_parse_word(ff_t *ff, const char *word)
{
    const char *name = ff_parse(ff, word, FF_TOKEN_WORD);
    if (!name)
        return NULL;
    ff_word_t *w = ff_dict_lookup(&ff->dict, name);
    if (!w)
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNDEFINED, "'%s' undefined.", name);
    return w;
}

/**
 * Create the word that a defining word (`:`, `create`, `variable`, …)
 * makes, named by the token that follows it.
 *
 * @param ff   Engine.
 * @param word The defining word, for messages.
 * @param op   Opcode of the new word.
 * @return The new word, already in the dictionary, or NULL with the
 *         error raised: no name, or the memory limit or the allocator
 *         refused it.
 */
static ff_word_t *ff_def_new(ff_t *ff, const char *word, ff_opcode_t op)
{
    const char *name = ff_parse(ff, word, FF_TOKEN_WORD);
    if (!name)
        return NULL;

    size_t cost = ff_dict_word_cost(name);
    if (!ff_mem_fits(&ff->dict.mem, cost))
        ff_mem_refuse(&ff->dict.mem, cost, false);
    else
    {
        if (ff_dict_lookup(&ff->dict, name))
            ff_tracef(ff, FF_SEV_WARNING | FF_ERR_NON_UNIQUE,
                      "'%s' isn't unique.", name);
        if (!ff_dict_append(&ff->dict, ff_word_new(name, NULL, op, NULL)))
            ff_mem_refuse(&ff->dict.mem, cost, true);
    }
    if (ff_mem_check(ff))
        return NULL;
    return ff_dict_top(&ff->dict);
}

/**
 * Find a running word among the user words from @p index on, which
 * `forget` would remove. A word runs while it is ff::cur_word or saved as
 * the caller's in a return frame on R — including the frame ff_exec()
 * pushes, so a word that called `evaluate` or `catch` counts — and the
 * code of a word being run lies in its own heap, or in that of an earlier
 * word (a `does>` clause), which would go too. Other cells on R that
 * happen to equal a word's address only make the check refuse more.
 *
 * @param ff    Engine.
 * @param index Index in ff::dict of the first word that would go.
 * @return The first running word found, or NULL.
 */
static const ff_word_t *ff_word_running_from(const ff_t *ff, size_t index)
{
    const ff_dict_t *d = &ff->dict;
    for (size_t i = index; i < d->count; ++i)
    {
        const ff_word_t *w = d->words[i];
        if (w == ff->cur_word)
            return w;
        for (size_t r = 0; r < ff->r_stack.top; ++r)
            if (ff->r_stack.data[r] == (ff_int_t)(intptr_t)w)
                return w;
    }
    return NULL;
}

/**
 * Find a word the host registered (ff_register()) among the user words
 * from @p index on, which `forget` would remove: the host's words stay,
 * as built-ins do.
 *
 * @param ff    Engine.
 * @param index Index in ff::dict of the first word that would go.
 * @return The first such word, or NULL.
 */
static const ff_word_t *ff_word_host_from(const ff_t *ff, size_t index)
{
    for (size_t i = index; i < ff->dict.count; ++i)
        if (ff->dict.words[i]->flags & FF_WORD_HOST)
            return ff->dict.words[i];
    return NULL;
}

/**
 * Begin a colon definition (`:`).
 *
 * @param ff Engine.
 * @return false, with the error raised, if its word couldn't be made.
 */
static bool ff_def_begin(ff_t *ff)
{
    ff_word_t *w = ff_def_new(ff, ":", FF_OP_NONE);
    if (!w)
        return false;
    ff->compiling = w;
    ff->def_depth = ff->eval_depth;
    ff->n_cf = 0;
    ff->state |= FF_STATE_COMPILING;
    return true;
}

/**
 * `postpone name` / `compile name`: append a word's compilation
 * semantics to the definition being compiled. For an immediate word,
 * `postpone` compiles a call, so it runs when this definition runs; for
 * any other word — and for every word under `compile` — it compiles
 * POSTPONE_RUNTIME, which compiles a call to the word when this
 * definition runs (deferred by one level).
 *
 * @param ff        Engine.
 * @param word      `postpone` or `compile`, for messages.
 * @param defer_all Defer immediate words too (`compile`).
 * @return false, with the error raised, on failure.
 */
static bool ff_postpone(ff_t *ff, const char *word, bool defer_all)
{
    ff_word_t *w = ff_parse_word(ff, word);
    if (!w)
        return false;
    ff_heap_t *h = &ff->compiling->heap;
    if ((w->flags & FF_WORD_IMMEDIATE) && !defer_all)
        ff_heap_compile_word(h, w);
    else
    {
        ff_heap_compile_op(h, FF_OP_POSTPONE_RUNTIME);
        ff_heap_compile_int(h, (ff_int_t)(intptr_t)w);
    }
    return !ff_mem_check(ff);
}

/**
 * Drop the compile-time `{` scope records, freeing their input names,
 * and leave signature mode.
 *
 * @param ff Engine.
 */
static void ff_csig_clear(ff_t *ff)
{
    while (ff->n_csig > 0)
    {
        ff_csig_t *cs = &ff->csig[--ff->n_csig];
        for (int i = 0; i < cs->nargs; i++)
            free(cs->names[i]);
    }
    ff->state &= ~FF_STATE_SIG_PENDING;
    ff->tokenizer.state &= ~FF_TOK_STATE_SIG;
}

/**
 * Abandon the definition being compiled, if any: remove its word, drop
 * the compiler's records for it and leave compile state.
 *
 * @param ff Engine.
 */
static void ff_def_abandon(ff_t *ff)
{
    ff_word_t *w = ff->compiling;
    ff->compiling = NULL;
    ff->n_cf = 0;
    ff_csig_clear(ff);
    ff->state &= ~FF_STATE_COMPILING;
    if (w)
        ff_dict_remove(&ff->dict, w);
}

/**
 * Clean up the compiler after an exception that unwinds out of the
 * evaluation at depth ff->eval_depth.
 *
 * The rest of that input is discarded, so a `{` signature being read
 * from it ends. A definition begun at this depth or deeper is abandoned.
 * One begun further out stays open: the exception is settled before it
 * gets there — an `evaluate` run from `[ ]` pushes the code, and
 * compilation carries on.
 *
 * @param ff Engine.
 */
static void ff_def_unwind(ff_t *ff)
{
    ff->state &= ~FF_STATE_SIG_PENDING;
    ff->tokenizer.state &= ~FF_TOK_STATE_SIG;
    if (ff->compiling && ff->def_depth >= ff->eval_depth)
        ff_def_abandon(ff);
}

/**
 * Open a control structure: push its record.
 *
 * @param ff     Engine.
 * @param kind   Record type.
 * @param opener Word opening it.
 * @param pos    Heap index the record refers to (see ff_cf::pos).
 * @return false, with the error raised, if structures nest too deeply.
 */
static bool ff_cf_push(ff_t *ff, ff_cf_kind_t kind, const char *opener,
                       size_t pos)
{
    if (ff->n_cf >= FF_CF_DEPTH)
    {
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_MALFORMED,
                  "Control structures nested deeper than %d.", FF_CF_DEPTH);
        return false;
    }
    ff_cf_t *c = &ff->cf[ff->n_cf++];
    c->kind   = kind;
    c->opener = opener;
    c->pos    = pos;
    c->scope  = ff->n_csig;
    return true;
}

/**
 * Check that @p closer can close the innermost open control structure:
 * that there is one, that it was opened inside the current `{ }` scope,
 * and that it is of kind @p kind.
 *
 * @param ff     Engine.
 * @param kind   Kind @p closer closes.
 * @param closer Word closing it.
 * @param expect What @p closer pairs with, for the message when none is open.
 * @return The record, still on the stack, or NULL with the error raised.
 */
static ff_cf_t *ff_cf_top(ff_t *ff, ff_cf_kind_t kind, const char *closer,
                          const char *expect)
{
    if (ff->n_cf == 0)
    {
        ff_raise(ff, FF_THROW_CS_MISMATCH, FF_SEV_ERROR | FF_ERR_MALFORMED,
                 "'%s' without '%s'.", closer, expect);
        return NULL;
    }
    ff_cf_t *c = &ff->cf[ff->n_cf - 1];
    if (c->scope != ff->n_csig)
    {
        ff_raise(ff, FF_THROW_CS_MISMATCH, FF_SEV_ERROR | FF_ERR_MALFORMED,
                 "'%s' can't close the '%s' outside this '{' scope.",
                 closer, c->opener);
        return NULL;
    }
    if (c->kind != kind)
    {
        ff_raise(ff, FF_THROW_CS_MISMATCH, FF_SEV_ERROR | FF_ERR_MALFORMED,
                 "'%s' can't close '%s'.", closer, c->opener);
        return NULL;
    }
    return c;
}

/**
 * ff_cf_top(), then pop the record. The pointer stays valid until the
 * next ff_cf_push().
 *
 * @param ff     Engine.
 * @param kind   Kind @p closer closes.
 * @param closer Word closing it.
 * @param expect What @p closer pairs with.
 * @return The popped record, or NULL with the error raised.
 */
static ff_cf_t *ff_cf_pop(ff_t *ff, ff_cf_kind_t kind, const char *closer,
                          const char *expect)
{
    ff_cf_t *c = ff_cf_top(ff, kind, closer, expect);
    if (c)
        ff->n_cf--;
    return c;
}

/**
 * Compile a SCOPE_UNWIND for each open `{` scope numbered above @p to, up
 * to @p from, innermost first. A jump out of them then leaves each one as
 * its `}` would, outputs checked; without it the barrier stayed raised in
 * the caller.
 *
 * @param ff   Engine.
 * @param from Innermost scope to leave (a count of open scopes).
 * @param to   Scopes up to this one stay open.
 */
static void ff_compile_scope_unwind(ff_t *ff, int from, int to)
{
    ff_heap_t *h = &ff->compiling->heap;
    for (int s = from; s > to; --s)
    {
        const ff_csig_t *cs = &ff->csig[s - 1];
        ff_heap_compile_op(h, FF_OP_SCOPE_UNWIND);
        ff_heap_compile_int(h, FF_SCOPE_PACK_EXIT(cs->nargs, cs->nouts,
                                                  cs->var_out));
    }
}

/**
 * Compile a call to @p w into the definition being compiled.
 *
 * `exit` and `leave` jump out of what encloses them, so they first close
 * it as its closer would at run time: each `{ }` scope with a
 * SCOPE_UNWIND, and for `exit` each counted loop with an UNLOOP. An
 * `exit` from a loop used to return through the loop's parameters as if
 * they were a return frame. `does>` also ends the running word, so it
 * may not stand where that would skip a loop's or a scope's cleanup.
 *
 * The built-ins whose code carries a cell the compiler fills in —
 * `(lit)`'s value, `branch`'s offset, `(xdo)`'s exit address — can't be
 * compiled by name: with no such cell, they took the next word's code
 * for it, and jumped by it.
 *
 * @param ff Engine.
 * @param w  Word to call.
 * @return false, with the error raised, if @p w can't be compiled here.
 */
static bool ff_compile_call(ff_t *ff, const ff_word_t *w)
{
    ff_heap_t *h = &ff->compiling->heap;

    ff_op_layout_t layout = ff_opcode_layout(w->opcode);
    if (layout != FF_OP_LAYOUT_NONE && layout != FF_OP_LAYOUT_WORD)
    {
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_MALFORMED,
                  "'%s' is internal: only the compiler compiles it.", w->name);
        return false;
    }

    if (w->opcode == FF_OP_EXIT)
    {
        int s = ff->n_csig;
        for (int i = ff->n_cf - 1; i >= 0; --i)
        {
            if (ff->cf[i].kind != FF_CF_DO)
                continue;
            ff_compile_scope_unwind(ff, s, ff->cf[i].scope);
            s = ff->cf[i].scope;
            ff_heap_compile_op(h, FF_OP_UNLOOP);
        }
        ff_compile_scope_unwind(ff, s, 0);
    }
    else if (w->opcode == FF_OP_LEAVE)
    {
        int i = ff->n_cf;
        while (i > 0 && ff->cf[i - 1].kind != FF_CF_DO)
            --i;
        if (i == 0)
        {
            ff_raise(ff, FF_THROW_CS_MISMATCH, FF_SEV_ERROR | FF_ERR_MALFORMED,
                     "'leave' outside 'do' ... 'loop'.");
            return false;
        }
        ff_compile_scope_unwind(ff, ff->n_csig, ff->cf[i - 1].scope);
    }
    else if (w->opcode == FF_OP_DOES)
    {
        for (int i = 0; i < ff->n_cf; ++i)
            if (ff->cf[i].kind == FF_CF_DO)
            {
                ff_raise(ff, FF_THROW_CS_MISMATCH,
                         FF_SEV_ERROR | FF_ERR_MALFORMED,
                         "'does>' inside '%s' ... 'loop'.", ff->cf[i].opener);
                return false;
            }
        if (ff->n_csig > 0)
        {
            ff_raise(ff, FF_THROW_CS_MISMATCH, FF_SEV_ERROR | FF_ERR_MALFORMED,
                     "'does>' inside a '{' scope.");
            return false;
        }
    }

    ff_heap_compile_word(h, w);
    return !ff_mem_check(ff);
}

/**
 * Reset the engine's transient state: the work of an uncaught ABORT, and
 * of ff_abort() between evaluations. Finished definitions are kept; one
 * still being compiled is abandoned.
 *
 * @param ff Engine.
 */
static void ff_reset(ff_t *ff)
{
    /* ff_def_abandon also frees the `{` scope records, whose names
       ff_scope_arg would otherwise go on resolving. */
    ff_def_abandon(ff);

    ff->stack.top = 0;
    ff->r_stack.top = 0;
    ff->ip = NULL;
    ff->state = 0;
    ff->tokenizer.state = 0;
    ff->cur_word = NULL;

    ff->n_scopes = 0;
    ff->stack.floor = 0;
    /* Anything still pointing into the transient-string arena becomes
       garbage — but we just cleared the data and return stacks, so there
       is nothing to dangle. */
    ff_pad_release(ff);
    /* A refusal nobody raised is stale now. */
    ff->dict.mem.failed = false;
}

/**
 * Settle the in-flight exception, if any, at an API boundary (ff_eval,
 * ff_load, or ff_exec called by the host) and return what the call
 * reports.
 *
 * A catchable exception stops at every boundary. An uncatchable one (host
 * abort, watchdog, QUIT) stops only at the outermost, so the evaluation
 * that re-entered the interpreter below it keeps unwinding. QUIT ends the
 * evaluation without being an error, so it reports FF_OK; an ABORT that
 * nothing caught resets the engine once it reaches the top.
 * ff::throw_code keeps the settled code for `evaluate` / `load` to push.
 *
 * @param ff        Engine.
 * @param outermost No evaluation or execution encloses this call.
 * @return Bare FF_ERR_* code for the call to return.
 */
static ff_error_t ff_settle(ff_t *ff, bool outermost)
{
    if (!(ff->state & FF_STATE_THROWN))
        return FF_OK;

    ff_int_t code = ff->throw_code;
    ff_error_t ec = code == FF_THROW_QUIT ? FF_OK : FF_ERR_CODE(ff->error);
    if (ff_throw_is_fatal(code) && !outermost)
        return ec;

    ff->state &= ~FF_STATE_THROWN;
    if (outermost && (code == FF_THROW_ABORT || code == FF_THROW_ABORTQ))
        ff_reset(ff);
    return ec;
}

/**
 * Finish a `{` signature at its closing `)`: emit FF_OP_SCOPE_ENTER and
 * hand the token stream back to the evaluator.
 *
 * @param ff Engine.
 * @param cs Signature record being closed.
 * @param ec Set to the raised error on failure.
 * @return false on a malformed signature.
 */
static bool ff_sig_finish(ff_t *ff, ff_csig_t *cs, ff_error_t *ec)
{
    /* `( ... -- c )` is rejected rather than silently unchecked: once an
       unknown number of cells has been consumed from below, there is no
       reference point left to verify an output count against, so the
       declaration could only ever be a comment — and a stack comment
       that lies is exactly what this construct exists to eliminate. */
    if (cs->var_in && !cs->var_out)
    {
        *ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_SCOPE_SIG,
                        "'( ... -- )' with a counted output: an output count "
                        "can't be verified once inputs are variadic. "
                        "Declare '-- ...' instead.");
        return false;
    }
    if (cs->nouts > 255)
    {
        *ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_SCOPE_SIG,
                        "Scope declares too many outputs (max 255).");
        return false;
    }

    ff_word_t *w = ff->compiling;
    ff_heap_t *h = &w->heap;

    /* Record the signature against the offset of the SCOPE_ENTER cell
       before emitting it, so `see` can render the scope back with its
       names. Failure to record is not fatal — the word still runs, it
       just decompiles without names. */
    ff_word_add_sig(w, h->size, cs->text);

    ff_heap_compile_op(h, FF_OP_SCOPE_ENTER);
    ff_heap_compile_int(h, FF_SCOPE_PACK_ENTER(cs->nargs, cs->var_in));
    ff_heap_inhibit_peephole(h);

    ff->state &= ~FF_STATE_SIG_PENDING;
    ff->tokenizer.state &= ~FF_TOK_STATE_SIG;
    return true;
}

/**
 * Consume one token of a `{ ( a b -- c )` signature.
 *
 * @param ff Engine.
 * @param tk Token text.
 * @param ec Set to the raised error on failure.
 * @return false on a malformed signature.
 */
static bool ff_sig_token(ff_t *ff, const char *tk, ff_error_t *ec)
{
    ff_csig_t *cs = &ff->csig[ff->n_csig - 1];

    /* Rebuild the signature source as it streams past, for `see`. A
       token too long to fit just truncates the recorded text; it must
       not fail the compile, since this is documentation, not semantics. */
    {
        size_t n = strlen(tk);
        if ((size_t)cs->text_len + n + 2 <= sizeof(cs->text))
        {
            if (cs->text_len)
                cs->text[cs->text_len++] = ' ';
            memcpy(cs->text + cs->text_len, tk, n);
            cs->text_len += (int)n;
            cs->text[cs->text_len] = '\0';
        }
    }

    switch (cs->phase)
    {
        case FF_CSIG_EXPECT_OPEN:
            if (strcmp(tk, "(") != 0)
            {
                *ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_SCOPE_SIG,
                                "'{' must be followed by '( … -- … )'; got '%s'.",
                                tk);
                return false;
            }
            cs->phase = FF_CSIG_INPUTS;
            return true;

        case FF_CSIG_INPUTS:
            if (strcmp(tk, "--") == 0)
            {
                cs->phase = FF_CSIG_OUTPUTS;
                return true;
            }
            if (strcmp(tk, ")") == 0)
            {
                *ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_SCOPE_SIG,
                                "Scope signature needs a '--'.");
                return false;
            }
            if (strcmp(tk, "...") == 0)
            {
                /* Named inputs compile to a fixed offset below the
                   barrier. '...' is permission to pop below it, which
                   would leave those names dangling — so the two can't
                   be combined. */
                if (cs->nargs > 0)
                {
                    *ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_SCOPE_SIG,
                                    "'...' can't be combined with named inputs.");
                    return false;
                }
                cs->var_in = true;
                return true;
            }
            if (cs->var_in)
            {
                *ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_SCOPE_SIG,
                                "Named input '%s' can't follow '...'.", tk);
                return false;
            }
            if (cs->nargs >= FF_SCOPE_ARGS_MAX)
            {
                *ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_SCOPE_SIG,
                                "Scope declares more than %d inputs.",
                                FF_SCOPE_ARGS_MAX);
                return false;
            }
            cs->names[cs->nargs] = ff_strdup(tk);
            if (!cs->names[cs->nargs])
            {
                *ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_OOM,
                                "Out of memory.");
                return false;
            }
            cs->nargs++;
            return true;

        case FF_CSIG_OUTPUTS:
        default:
            if (strcmp(tk, ")") == 0)
                return ff_sig_finish(ff, cs, ec);
            if (strcmp(tk, "--") == 0)
            {
                *ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_SCOPE_SIG,
                                "Scope signature has more than one '--'.");
                return false;
            }
            if (strcmp(tk, "...") == 0)
            {
                cs->var_out = true;
                return true;
            }
            if (cs->var_out)
            {
                *ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_SCOPE_SIG,
                                "Named output '%s' can't follow '...'.", tk);
                return false;
            }
            /* Output names are documentation; only the count is enforced. */
            cs->nouts++;
            return true;
    }
}

/**
 * Resolve @p name against the innermost open scope's named inputs.
 *
 * Only the innermost scope is consulted, by design: a scope's whole
 * contract is that the only way past its barrier is through the names it
 * declares. An enclosing scope's names would compile to an offset from
 * the *enclosing* floor, which isn't reachable from the inner one — and
 * making them visible would mean a signature no longer describes
 * everything the scope touches. Pass what an inner scope needs into it.
 *
 * @param ff   Engine.
 * @param name Token to resolve.
 * @return FF_OP_ARG operand (1 = last-declared input), or 0 if not a scope input.
 */
static int ff_scope_arg(const ff_t *ff, const char *name)
{
    if (ff->n_csig <= 0)
        return 0;

    const ff_csig_t *cs = &ff->csig[ff->n_csig - 1];
    for (int i = 0; i < cs->nargs; i++)
        if (strcmp(cs->names[i], name) == 0)
            return cs->nargs - i;

    return 0;
}

/** @copydoc ff_eval */
ff_error_t ff_eval(ff_t *ff, const char *src)
{
    const bool host = ff_idle(ff);
    if (host)
        ff_host_enter(ff);

    if (!src
            || !*src)
        return FF_OK;

    /* Nothing new runs while an exception unwinds — possible only if a
       native word raised an error and then evaluated more source. The
       exception carries on once the native returns. */
    if (ff->state & FF_STATE_THROWN)
        return ff->throw_code == FF_THROW_QUIT ? FF_OK : FF_ERR_CODE(ff->error);

    /* The tokenizer's token-start offset indexes ff->input (it locates
       errors), so it is saved and restored with it: after a nested
       `evaluate` returns, errors must point into the outer source again. */
    const char *prev_input = ff->input;
    int prev_pos = ff->input_pos;
    int prev_tok_pos = ff->tokenizer.pos;
    ff->input = src;
    ff->input_pos = 0;
    ff->tokenizer.pos = 0;

    ff_eval_enter(ff);

    int pos = 0;
    ff_dict_t *d = &ff->dict;
    ff_tokenizer_t *t = &ff->tokenizer;
    ff_error_t ec = FF_OK;

    for (;;)
    {
        /* Compiling the previous token may have needed memory it didn't
           get (see ff_mem_p.h): stop before anything builds on it. */
        if (ff_unlikely(ff->dict.mem.failed))
        {
            ff_mem_check(ff);
            goto out;
        }

        ff_token_t tok = ff_tokenizer_next(t, src, &pos);

        /* A token that overran FF_TOKEN_SIZE was silently truncated by the
           lexer; a truncated name could resolve to a different word and a
           truncated number mis-parse, so reject it outright. */
        if (t->truncated)
        {
            ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_MALFORMED,
                           "Token longer than %d bytes.", FF_TOKEN_SIZE - 1);
            goto out;
        }
        if (t->bad_escape)
        {
            ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_MALFORMED,
                           "Invalid escape sequence in string literal.");
            goto out;
        }

        /* Signature mode: `{` owns the token stream until its `)`. Handled
           ahead of the kind dispatch because a signature is a list of
           names, not code — a name that happens to lex as a number
           (`( a 2 -- b )`) has to be rejected, not compiled as a literal. */
        /* FF_TOKEN_NULL falls through to the switch below, leaving
           FF_STATE_SIG_PENDING set: a signature may span ff_eval calls,
           as an open `(` comment may. ffsh feeds one line per call, so
           anything else would make a multi-line signature work in a
           loaded file but not at the prompt. */
        if ((ff->state & FF_STATE_SIG_PENDING) && tok != FF_TOKEN_NULL)
        {
            if (tok != FF_TOKEN_WORD)
            {
                ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_SCOPE_SIG,
                               "'%s' is not a name a scope signature can bind.",
                               t->token);
                goto out;
            }
            if (!ff_sig_token(ff, t->token, &ec))
                goto out;
            continue;
        }

        switch (tok)
        {
            case FF_TOKEN_NULL:
                goto out;

            case FF_TOKEN_WORD:
                if ((ff->state & FF_STATE_COMPILING)
                        && ff_scope_arg(ff, t->token) > 0)
                {
                    /* A named scope input. Resolved here, ahead of the
                       dictionary, so the name is bound lexically and
                       shadows any word of the same spelling for the
                       body of the scope. Compiles to an indexed read
                       below the barrier — no dictionary entry is ever
                       created, so nothing can collide and nothing needs
                       cleaning up at `}`. */
                    ff_heap_t *h = &ff->compiling->heap;
                    ff_heap_compile_op(h, FF_OP_ARG);
                    ff_heap_compile_int(h, ff_scope_arg(ff, t->token));
                }
                else
                {
                    ff_word_t *w = ff_dict_lookup(d, t->token);
                    if (!w)
                    {
                        ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNDEFINED,
                                       "'%s' undefined.", t->token);
                        goto out;
                    }
                    /* Compiling, a word is compiled unless it is
                       immediate; otherwise it runs. */
                    if ((ff->state & FF_STATE_COMPILING)
                            && !(w->flags & FF_WORD_IMMEDIATE))
                    {
                        if (!ff_compile_call(ff, w))
                            goto out;
                    }
                    else
                    {
                        /* A word that parses (`:`, `'`, `."`, …) reads
                           the tokens after it from here. */
                        ff->input = src;
                        ff->input_pos = pos;
                        /* An exception — error, THROW, ABORT, QUIT —
                           discards the rest of the input: `out`
                           settles it into the return code. */
                        if (!ff_exec(ff, w))
                            goto out;
                        pos = ff->input_pos;
                    }
                }
                break;

            case FF_TOKEN_INTEGER:
                if (ff->state & FF_STATE_COMPILING)
                    ff_heap_compile_lit(&ff->compiling->heap, t->integer_val);
                else if (ff_eval_room(ff, &ec))
                    ff_stack_push(&ff->stack, t->integer_val);
                else
                    goto out;
                break;

            case FF_TOKEN_REAL:
                if (ff->state & FF_STATE_COMPILING)
                {
                    ff_heap_compile_op(&ff->compiling->heap, FF_OP_FLIT);
                    ff_heap_compile_real(&ff->compiling->heap, t->real_val);
                }
                else if (ff_eval_room(ff, &ec))
                    ff_stack_push_real(&ff->stack, t->real_val);
                else
                    goto out;
                break;

            case FF_TOKEN_STRING:
                if (ff->state & FF_STATE_COMPILING)
                {
                    ff_heap_compile_op(&ff->compiling->heap, FF_OP_STRLIT);
                    ff_heap_compile_str(&ff->compiling->heap,
                                        t->token, t->token_len);
                }
                else
                {
                    if (!ff_eval_room(ff, &ec))
                        goto out;
                    /* Intern into the bump arena; the pushed pointer
                       stays stable for the engine's lifetime. */
                    char *dst = ff_pad_intern(ff, t->token, t->token_len);
                    if (!dst)
                    {
                        ff_mem_check(ff);
                        goto out;
                    }
                    ff_stack_push_ptr(&ff->stack, dst);
                }
                break;
        }
    }

out:
    /* The last token compiled may have been refused memory too. (With
       an exception already in flight this just clears the record.) */
    ff_mem_check(ff);

    /* An exception discards the rest of this input, and with it a `{`
       signature being read and a definition begun at this depth — with
       its scope and control-flow records. On the clean-exit path they
       carry over: a definition, or an open `{` signature, may continue in
       the next ff_eval call. */
    if (ff->state & FF_STATE_THROWN)
        ff_def_unwind(ff);

    /* Single restore-and-return point: every clean exit (FF_TOKEN_NULL)
       and every error path lands here so the input/input_pos snapshot
       is rolled back exactly once. */
    /* An unterminated string literal makes the lexer return NULL like a
       clean EOF; the flag distinguishes them so it isn't silently dropped.
       It is cleared even when another error already won (a bad escape or
       an overlong token inside the same literal): left set, it would fail
       the *next*, perfectly good, evaluation. */
    if (ff->tokenizer.state & FF_TOK_STATE_STRING)
    {
        ff->tokenizer.state &= ~FF_TOK_STATE_STRING;
        if (!(ff->state & FF_STATE_THROWN))
            ff_tracef(ff, FF_SEV_ERROR | FF_ERR_RUN_STRING,
                      "Unterminated string literal.");
    }

    ff->input = prev_input;
    ff->input_pos = prev_pos;
    ff->tokenizer.pos = prev_tok_pos;

    /* Every error was raised as an exception; this boundary turns it into
       the return code (or, for an uncatchable one inside a nested call,
       reports it and lets it keep unwinding). */
    ec = ff_settle(ff, host);
    ff_eval_leave(ff);
    return host ? ff_host_leave(ff, ec) : ec;
}

/* ===================================================================
 * Output helpers for words that format reals. They stay out of
 * ff_exec() — FF_NOINLINE, or the compiler takes them back in: a
 * character array among its locals made it build the whole dispatch
 * loop differently, about 5 % more instructions for every word run
 * (8 % inlined from here), measured with cachegrind.
 * =================================================================== */

/**
 * Print @p r as `f.` does, with '.' as the decimal point.
 * @param ff Engine.
 * @param r  Value.
 */
static FF_NOINLINE void ff_print_real(ff_t *ff, ff_real_t r)
{
    char num[40];
    ff_real_format(num, sizeof(num), "%g", (double)r);
    ff_printf(ff, "%s", num);
}

/**
 * Raise `fix`'s error for a real whose integer part doesn't fit a cell.
 * @param ff Engine.
 * @param r  Value.
 */
static FF_NOINLINE void ff_fix_range_error(ff_t *ff, ff_real_t r)
{
    char num[40];
    ff_real_format(num, sizeof(num), "%g", (double)r);
    ff_raise(ff, FF_THROW_BAD_ARG, FF_SEV_ERROR | FF_ERR_MALFORMED,
             "fix: %s doesn't fit a cell.", num);
}

/**
 * Print the data stack as `.s` does: a table of every cell as a decimal,
 * hex and real number, a character and a pointer.
 * @param ff Engine, its stack in memory.
 */
static FF_NOINLINE void ff_print_stack(ff_t *ff)
{
    const ff_stack_t *S = &ff->stack;
    ft_table_t *tbl = ft_create_table();
    ft_set_border_style(tbl, FT_SOLID_ROUND_STYLE);
    ft_set_cell_prop(tbl, 0, FT_ANY_COLUMN, FT_CPROP_ROW_TYPE, FT_ROW_HEADER);
    ft_set_cell_prop(tbl, 0, FT_ANY_COLUMN, FT_CPROP_CELL_TEXT_STYLE, FT_TSTYLE_BOLD);
    ft_set_cell_prop(tbl, FT_ANY_ROW, FT_ANY_COLUMN, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_RIGHT);
    ft_set_cell_prop(tbl, FT_ANY_ROW, 0, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_CENTER);
    ft_set_cell_prop(tbl, FT_ANY_ROW, 4, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_CENTER);
    ft_set_cell_prop(tbl, 0, FT_ANY_COLUMN, FT_CPROP_TEXT_ALIGN, FT_ALIGNED_CENTER);
    ft_u8write_ln(tbl, "#", "Dec", "Hex", "Real", "ASCII", "Ptr");
    for (size_t n = 0; n < S->top; ++n)
    {
        ff_int_t v = S->data[n];
        ff_real_t r;
        memcpy(&r, &v, sizeof(r));
        char c = (v > 0 && v < 0xFF && isprint((int)v)) ? (char)v : ' ';
        char num[40];
        ff_real_format(num, sizeof(num), "%g", (double)r);
        ft_u8printf_ln(tbl, "%zu|%" FF_PRIdCELL "|%" FF_PRIXCELL "|%s|%c|%p",
                       n, v, (ff_uint_t)v, num, c, (void *)(intptr_t)v);
    }
    ff_printf(ff, "\n%s", (const char *)ft_to_u8string(tbl));
    ft_destroy_table(tbl);
}

/** @copydoc ff_exec */
bool ff_exec(ff_t *ff, ff_word_t *w)
{
    const bool host = ff_idle(ff);
    if (host)
        ff_host_enter(ff);

    /* Nothing new starts while an exception unwinds (a native word that
       raised an error and then called back in). */
    if (ff->state & FF_STATE_THROWN)
        return false;

    /* An error like any other: from the host, recorded with the engine
       left ready; from a native word, it stops the word that called it. */
    if (!w)
    {
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_BAD_PTR, "No word to execute.");
        return false;
    }

    ff_stack_t *S = &ff->stack;
    ff_stack_t *R = &ff->r_stack;
    ff_bt_stack_t *BT = &ff->bt_stack;

    /* Local watchdog batch counter. Initialised before the
       early-goto-done path so the done block's flush is well-defined
       even when no dispatch ran. */
    int wd_tick = FF_WD_BATCH;

    /* Snapshot cur_word so an error-path `goto done` (which bypasses
       any pending EXITs) restores the caller's value. The clean-EXIT
       path also unwinds correctly because every NEST / DOES_RUNTIME
       saves cur_word into the return frame and EXIT pops it back. */
    ff_word_t *prev_cur_word = ff->cur_word;
    ff->cur_word = w;
    int bt_size = BT->top;

    /* ff->ip is handed back as it was found. A word that re-enters the
       interpreter — `evaluate`, `catch`, `load`, or a native word calling
       ff_eval() or ff_exec() — syncs its ip there first and reloads it
       after, so a nested run that left its own final ip (NULL) behind
       made the caller stop dead after the call, without an error. */
    ff_int_t *const entry_ip = ff->ip;

    /* An invocation leaves nothing behind. An error exit abandons the
       return frames and `{` scopes it opened, so the exit path cuts both
       back to their entry values: otherwise every runtime error leaks
       return-stack cells until the array overflows, and a failed scope
       keeps its barrier raised for all later code. A clean run has
       already balanced both, so the cut is a no-op there. */
    const size_t r_base      = R->top;
    const size_t floor_base  = S->floor;
    const size_t scopes_base = ff->n_scopes;
    ++ff->exec_depth;

    if (ff->state & (FF_STATE_TRACE | FF_STATE_BACKTRACE))
    {
        if (ff->state & FF_STATE_TRACE)
            ff_tracef(ff, FF_SEV_TRACE, "%s \xe2\x86\x92", w->name);
        if (ff->state & FF_STATE_BACKTRACE)
            ff_bt_stack_push(BT, w);
    }

    /* Run the word's stub — its call sequence then EXIT (ff_word_t::stub)
       — under a two-cell return frame [saved_ip, saved_cur_word] whose ip
       is NULL: the stub's EXIT pops it and lands on `done`. The cur_word
       slot hands the caller's value back. The push is checked like any
       other: `catch` and native words re-enter here once per level. */
    ff_int_t *ip = NULL;

    /* Push @p v onto R as a cell of kind @p k (ff_rkind_t), which an
       FF_SAFE_MEM build records for _FF_RKIND below. Every push onto R
       goes through here, so a cell's kind is always its latest push's. */
#if FF_SAFE_MEM
    #define _FF_RPUSH(v, k) \
        do { ff->r_kind[R->top] = (uint8_t)(k); ff_stack_push(R, (v)); } while (0)
#else
    #define _FF_RPUSH(v, k)  ff_stack_push(R, (v))
#endif

    if (ff_unlikely(R->top + 2 > FF_STACK_SIZE))
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_RSTACK_OVER,
                  "Return stack overflow: %d item(s) would not fit.", 2);
    else
    {
        _FF_RPUSH(0, FF_RK_IP);
        _FF_RPUSH((ff_int_t)(intptr_t)prev_cur_word, FF_RK_WORD);
        ip = w->stub;
    }

    /* Top-of-stack register cache. While dispatching, the topmost data
       stack value lives in `tos` (when S->top > 0); the in-memory slot at
       S->data[S->top - 1] is treated as scratch and may be stale until the
       next _FF_SYNC_TOS / _FF_SYNC. This shaves a load+store off every
       arithmetic operation that takes or returns TOS in place. Pure pushes
       still have to write the displaced TOS back, so the optimization
       targets compute-heavy bytecode rather than push-heavy code.

       Initialised *before* the `if (!ip) goto done` early exit (the
       return frame was refused): `done` writes `tos` back to
       S->data[top - 1], and reading the current top here makes that
       write-back a harmless no-op. */
    ff_int_t tos = S->top
                        ? S->data[S->top - 1]
                        : 0;

    if (!ip)
        goto done;

    /* Scope-barrier register cache. Mirrors S->floor for the duration of
       dispatch so _FF_SL costs a register subtract rather than a second
       memory load. Written only by SCOPE_ENTER / SCOPE_EXIT. */
    size_t floor = S->floor;

    #define _FF_SYNC_TOS()   do { if (S->top) S->data[S->top - 1] = tos; } while (0)
    #define _FF_LOAD_TOS()   do { if (S->top) tos = S->data[S->top - 1]; } while (0)

    #define _FF_SYNC()    do { ff->ip = ip; S->floor = floor; _FF_SYNC_TOS(); } while (0)
    #define _FF_RESTORE() do { ip = ff->ip; floor = S->floor; _FF_LOAD_TOS(); } while (0)

    /* In-register convenience accessors used by case bodies. _FF_TOS is the
       cached TOS value (lvalue), _FF_NOS / _FF_SAT(i) reach into memory below
       the cache. _FF_SAT(0) is invalid — use _FF_TOS for index 0. */
    #define _FF_TOS          tos
    #define _FF_NOS          (S->data[S->top - 2])
    #define _FF_SAT(i)       (S->data[S->top - 1 - (i)])

    /* Stack-mutating helpers used by case bodies. _FF_PUSH stores the
       displaced TOS to memory before bringing in the new top; _FF_DROP /
       _FF_DROPN reload TOS from memory if any items remain. */
    #define _FF_PUSH(x) \
        do { \
            if (S->top) S->data[S->top - 1] = tos; \
            tos = (x); \
            ++S->top; \
        } while (0)
    #define _FF_PUSH_PTR(p)  _FF_PUSH((ff_int_t)(intptr_t)(p))
    #define _FF_PUSH_REAL(r) \
        do { \
            if (S->top) S->data[S->top - 1] = tos; \
            ff_set_real(&tos, (r)); \
            ++S->top; \
        } while (0)
    #define _FF_DROP() \
        do { \
            if (--S->top) tos = S->data[S->top - 1]; \
        } while (0)
    #define _FF_DROPN(n) \
        do { \
            S->top -= (n); \
            if (S->top) tos = S->data[S->top - 1]; \
        } while (0)

    /* Dispatch-context validation. Unlike FF_SL/FF_SO/FF_RSL/FF_RSO in
       ff_p.h these `goto done` rather than `return`-ing, because ff_exec
       returns bool and must restore state before returning, and they read
       the register-cached `floor` rather than ff->stack.floor. These MUST
       track the ff_p.h twins' depth semantics exactly — edit both together
       (see the note on FF_SL in ff_p.h). */
    #define _FF_SL(n) \
        do { \
            if (ff_unlikely((int)(S->top - floor) < (int)(n))) \
            { \
                _FF_SYNC(); \
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_STACK_UNDER, \
                          "Stack underflow: %d item(s) expected.", (int)(n)); \
                goto done; \
            } \
        } while (0)
    #define _FF_SO(n) \
        do { \
            if (ff_unlikely((int)S->top + (int)(n) > FF_STACK_SIZE)) \
            { \
                _FF_SYNC(); \
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_STACK_OVER, \
                          "Stack overflow: %d item(s) would not fit.", (int)(n)); \
                goto done; \
            } \
        } while (0)
    #define _FF_RSL(n) \
        do { \
            if (ff_unlikely((int)R->top < (int)(n))) \
            { \
                _FF_SYNC(); \
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_RSTACK_UNDER, \
                          "Return stack underflow: %d item(s) expected.", (int)(n)); \
                goto done; \
            } \
        } while (0)
    #define _FF_RSO(n) \
        do { \
            if (ff_unlikely((int)R->top + (int)(n) > FF_STACK_SIZE)) \
            { \
                _FF_SYNC(); \
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_RSTACK_OVER, \
                          "Return stack overflow: %d item(s) would not fit.", (int)(n)); \
                goto done; \
            } \
        } while (0)
    #define _FF_COMPILING \
        do { \
            if (ff_unlikely(!(ff->state & FF_STATE_COMPILING))) \
            { \
                _FF_SYNC(); \
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_NOT_IN_DEF, \
                          "Compiler word outside definition."); \
                goto done; \
            } \
        } while (0)
    /* Words that act on the newest word, ff_dict_top(): `here`, `,`,
       `allot`, `immediate`, `does>`, … On a fresh engine there is none. */
    #define _FF_NEED_DEF \
        do { \
            if (ff_unlikely(!ff_dict_top(&ff->dict))) \
            { \
                _FF_SYNC(); \
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_NOT_IN_DEF, \
                          "No current definition."); \
                goto done; \
            } \
        } while (0)

    /* After a call out to C that can raise — a native word, a nested
       evaluation, a helper that reports through ff_tracef — stop at once
       if it did: the exception unwinds through this word too. */
    #define _FF_CHECK_THROWN() \
        do { \
            if (ff_unlikely(ff->state & FF_STATE_THROWN)) \
                goto done; \
        } while (0)

    /* After a case body that appended to a heap or made a word: an
       allocation the memory limit or the allocator refused (see
       ff_mem_p.h) ends the run here, before anything relies on what it
       didn't get. */
    #define _FF_CHECK_MEM() \
        do { \
            if (ff_unlikely(ff->dict.mem.failed)) \
            { \
                _FF_SYNC(); \
                ff_mem_check(ff); \
                goto done; \
            } \
        } while (0)

    /* _FF_CHECK_MEM for the word a defining word has just made: one whose
       storage couldn't be had goes again, rather than stay half-made. */
    #define _FF_CHECK_MEM_NEW(w) \
        do { \
            if (ff_unlikely(ff->dict.mem.failed)) \
            { \
                _FF_SYNC(); \
                ff_dict_remove(&ff->dict, (w)); \
                ff_mem_check(ff); \
                goto done; \
            } \
        } while (0)

    /* A size argument (`allot`, `array`, `fgets`, …) that is negative, or
       otherwise out of range for its word. */
    #define _FF_BAD_SIZE(cond, word, n) \
        do { \
            if (ff_unlikely(cond)) \
            { \
                _FF_SYNC(); \
                ff_raise(ff, FF_THROW_BAD_ARG, FF_SEV_ERROR | FF_ERR_MALFORMED, \
                         "%s: invalid size %" FF_PRIdCELL ".", (word), \
                         (ff_int_t)(n)); \
                goto done; \
            } \
        } while (0)

    /* Words that reach outside the engine run only if the host hasn't
       withheld them (ff_platform::deny). */
    #define _FF_NEED_CAP(cap, word) \
        do { \
            if (ff_unlikely(ff->platform.deny & (cap))) \
            { \
                _FF_SYNC(); \
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNSUPPORTED, \
                          "'%s' is not permitted.", (word)); \
                goto done; \
            } \
        } while (0)

    /* True while an operand-free opcode runs straight from a word's stub —
       the interpreter (via ff_exec) or `execute` running the word itself —
       rather than from compiled code. Valid only before any `ip++`. */
    #define _FF_RUNNING_DIRECT  (ip == ff->cur_word->stub + 1)

    /* Dispatch-context address check; gated by FF_SAFE_MEM. Compiles
       to nothing in the default build. See ff_p.h:FF_CHECK_ADDR for
       the word-fn variant. */
#if FF_SAFE_MEM
    #define _FF_CHECK_ADDR(addr, bytes) \
        do { \
            if (ff_unlikely(!ff_addr_valid(ff, (addr), (size_t)(bytes)))) \
            { \
                _FF_SYNC(); \
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_BAD_PTR, \
                          "Bad pointer: %p (size %zu).", \
                          (const void *)(addr), (size_t)(bytes)); \
                goto done; \
            } \
        } while (0)
    /* For memory about to be written: not bytecode, not a native's fn
       pointer (see ff_word_holds_data). */
    #define _FF_CHECK_WRITE(addr, bytes) \
        do { \
            if (ff_unlikely(!ff_addr_writable(ff, (addr), (size_t)(bytes)))) \
            { \
                _FF_SYNC(); \
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_BAD_PTR, \
                          "Bad pointer for a write: %p (size %zu).", \
                          (const void *)(addr), (size_t)(bytes)); \
                goto done; \
            } \
        } while (0)
    #define _FF_CHECK_XT(w) \
        do { \
            if (ff_unlikely(!ff_word_valid(ff, (w)))) \
            { \
                _FF_SYNC(); \
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_BAD_PTR, \
                          "Bad execution token: %p.", (const void *)(w)); \
                goto done; \
            } \
        } while (0)
    #define _FF_CHECK_STR(s) \
        do { \
            if (ff_unlikely(!ff_str_valid(ff, (const char *)(intptr_t)(s)))) \
            { \
                _FF_SYNC(); \
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_BAD_PTR, \
                          "Bad string: %p.", (const void *)(intptr_t)(s)); \
                goto done; \
            } \
        } while (0)
    /* A file stream the program opened; with std_too, stdin / stdout /
       stderr as well (everything but `fclose`). */
    #define _FF_CHECK_FILE(f, std_too) \
        do { \
            if (ff_unlikely(!ff_file_valid(ff, (const FILE *)(intptr_t)(f), \
                                           (std_too)))) \
            { \
                _FF_SYNC(); \
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_BAD_PTR, \
                          "Bad file stream: %p.", (const void *)(intptr_t)(f)); \
                goto done; \
            } \
        } while (0)
    /* `,`, `c,` and `allot` extend the newest word: only a data word, so
       no program can append to bytecode (see ff_word_holds_data). */
    #define _FF_CHECK_DATA_WORD(w) \
        do { \
            if (ff_unlikely(!ff_word_holds_data(w))) \
            { \
                _FF_SYNC(); \
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_BAD_PTR, \
                          "'%s' holds code, not data.", (w)->name); \
                goto done; \
            } \
        } while (0)
#else
    #define _FF_CHECK_ADDR(addr, bytes) ((void)0)
    #define _FF_CHECK_WRITE(addr, bytes) ((void)0)
    #define _FF_CHECK_XT(w)             ((void)0)
    #define _FF_CHECK_STR(s)            ((void)0)
    #define _FF_CHECK_FILE(f, std_too)  ((void)0)
    #define _FF_CHECK_DATA_WORD(w)      ((void)0)
#endif

    /* Watchdog: count back-branches and word calls, check for an
       async abort, and (every N opcodes) call the host's polling
       watchdog.

       The hot path is a local-counter decrement-and-test — no
       memory write, no atomic load, no flag check on the typical
       tick. Every FF_WD_BATCH ticks (256) we sync the local count
       to ff->opcodes_run, do the atomic abort load, and compare
       against the watchdog threshold.

       Async-abort latency goes from "next opcode" to "next 256
       opcodes" — sub-microsecond on this hardware, and well below
       the documented 65536-default watchdog interval. The
       FF_WD_BATCH constant is defined at file scope below so the
       local `wd_tick` declaration can reference it. */
    #define _FF_WATCHDOG_TICK() \
        do { \
            if (ff_unlikely(--wd_tick <= 0)) \
            { \
                wd_tick = FF_WD_BATCH; \
                ff->opcodes_run += FF_WD_BATCH; \
                if (ff_unlikely(FF_ABORT_LOAD(&ff->abort_requested))) \
                    goto _watchdog_abort; \
                if (ff->platform.watchdog \
                        && ff->opcodes_run >= ff->next_watchdog_at) \
                { \
                    _FF_SYNC(); \
                    ff_watchdog_action_t _wd = \
                        ff->platform.watchdog(ff->platform.context, \
                                              ff->opcodes_run); \
                    _FF_RESTORE(); \
                    uint32_t _step = ff->platform.watchdog_interval \
                                         ? ff->platform.watchdog_interval \
                                         : 65536; \
                    ff->next_watchdog_at = ff->opcodes_run + _step; \
                    if (_wd != FF_WD_CONTINUE) \
                        goto _watchdog_abort; \
                } \
            } \
        } while (0)

    /* Trusted-bytecode return-stack checks. Inside opcodes that the
       compiler emits in matched pairs (XDO ... XLOOP, etc.), the
       _FF_RSL_T underflow check can't fail in well-formed code and is
       elided when FF_R_TRUSTED is on (`i` / `j` / `leave` misused outside
       a loop then go unchecked). The unsuffixed _FF_RSL above stays live
       because it protects words (>R, R>, R@) that user code can
       stand-alone-misuse. There is deliberately no trusted variant of
       _FF_RSO: how deep NEST / DO / DOES_RUNTIME push depends on the
       program's recursion depth, so an overflow is always a
       user-reachable failure, never an engine bug. FF_SAFE_MEM keeps the
       checks: there the code isn't trusted. */
#if FF_R_TRUSTED && !FF_SAFE_MEM
    #define _FF_RSL_T(n)  ((void)0)
#else
    #define _FF_RSL_T(n)  _FF_RSL(n)
#endif

    /* FF_SAFE_MEM: cell @p i from the top of R (which holds more than
       @p i cells) must be of kind @p k, pushed by the interpreter for
       what reads it now. A return, `leave` and a loop's back edge check
       the cells they follow or change; one that finds a cell a program
       pushed with `>r`, or a frame it took apart, raises -25 instead of
       going wherever that cell says. */
#if FF_SAFE_MEM
    #define _FF_RKIND(i, k) \
        do { \
            if (ff_unlikely(ff->r_kind[R->top - 1 - (i)] != (k))) \
            { \
                _FF_SYNC(); \
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_RSTACK_IMBAL, \
                          "Return stack imbalance: >r without r>, " \
                          "or a return frame taken apart."); \
                goto done; \
            } \
        } while (0)
#else
    #define _FF_RKIND(i, k)  ((void)0)
#endif

    #define _FF_NEXT()    break

    for (;;)
    {
        switch (*ip++)
        {
            /* Structural escape hatch: external C word. The external word
               accesses the data stack through ff->stack only, so we must
               sync the cached TOS into memory before the call and reload
               it (the helper may have pushed/popped/replaced TOS). */
            case FF_OP_CALL:
                {
                    ff_word_fn fn = (ff_word_fn)(intptr_t)*ip++;
                    _FF_SYNC();
                    fn(ff);
                    _FF_RESTORE();
                }
                /* An error the native raised (ff_tracef) stops the word
                   that called it, as any other error does. */
                _FF_CHECK_THROWN();
                /* Anything the native ran hands ff->ip back (entry_ip). */
                assert(ip);
                _FF_NEXT();

            /* Built-in word bodies live in per-category headers that
               are included here so each case is inline. The headers
               reference the macros (_FF_NEXT, _FF_SYNC, _FF_RESTORE, _FF_SO,
               _FF_RSO, …), the `done` label, and local variables
               (S, R, BT, ip, tos, floor, ff) in this scope. */
            /* FF_IN_EXEC gates the dispatch fragments: each #error's out
               if included anywhere but here. */
            #define FF_IN_EXEC 1
            #include "ff_words_stack_p.h"
            #include "ff_words_stack2_p.h"
            #include "ff_words_math_p.h"
            #include "ff_words_ctrl_p.h"
            #include "ff_words_real_p.h"
            #include "ff_words_string_p.h"
            #include "ff_words_conio_p.h"
            #include "ff_words_heap_p.h"
            #include "ff_words_eval_p.h"
            #include "ff_words_debug_p.h"
            #include "ff_words_field_p.h"
            #include "ff_words_file_p.h"
            #include "ff_words_var_p.h"
            #include "ff_words_comp_p.h"
            #include "ff_words_scope_p.h"
            #include "ff_words_array_p.h"
            #include "ff_words_dict_p.h"
            #undef FF_IN_EXEC

            default:
                /* Trusted builds keep the unreachable hint so the
                   compiler can elide bounds checks on the dispatch
                   table. Safe builds turn it into a noisy error so a
                   wild opcode (heap corruption, stale ip) raises a
                   clean FF_ERR_BAD_OPCODE instead of UB. */
#if FF_SAFE_MEM
                _FF_SYNC();
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_BAD_OPCODE,
                          "Bad opcode 0x%llx.",
                          (unsigned long long)*(ip - 1));
                goto done;
#else
                FF_UNREACHABLE();
#endif
        }
    }


    /* --- Exit points --- */

_watchdog_abort:
    /* Watchdog or async ff_request_abort fired. Clear the flag (so the
       next evaluation starts fresh) and raise the one exception `catch`
       can't stop: it unwinds to the outermost evaluation, which returns
       FF_ERR_ABORTED. */
    _FF_SYNC();
    FF_ABORT_CLEAR(&ff->abort_requested);
    ff_raise(ff, FF_THROW_INTERRUPT, FF_SEV_ERROR | FF_ERR_ABORTED,
             "Aborted after %llu opcodes.",
             (unsigned long long)ff->opcodes_run);

done:
    /* Flush the local watchdog batch counter back into the engine's
       running total before returning, so a host that reads
       ff->opcodes_run after a failed run sees an accurate count. */
    ff->opcodes_run += (uint64_t)(FF_WD_BATCH - wd_tick);
    ff->ip = entry_ip;
    if (S->top) S->data[S->top - 1] = tos;
    /* Cut back to the entry state (see r_base). Only ever lowers R: a
       native word that popped below it is left to its own devices. */
    if (R->top > r_base)
        R->top = r_base;
    if (ff->n_scopes > scopes_base)
    {
        ff->n_scopes = scopes_base;
        S->floor = floor_base;
    }
    ff->cur_word = prev_cur_word;
    BT->top = bt_size;
    --ff->exec_depth;

    /* The run failed iff an exception is unwinding out of it. A host
       that called ff_exec directly has nothing above it to settle that
       exception, so it is settled here and the next call starts clean. */
    bool ok = !(ff->state & FF_STATE_THROWN);
    if (!ok && host)
    {
        /* A definition this call began goes with it. One the host is
           feeding in through ff_eval() calls stays open: this call is not
           part of that input. */
        if (ff->compiling && ff->def_depth == 0)
            ff_def_abandon(ff);
        (void)ff_settle(ff, true);
    }
    else if (host)
        (void)ff_host_leave(ff, FF_OK);
    return ok;

    #undef _FF_NEXT
    #undef _FF_SYNC
    #undef _FF_RESTORE
    #undef _FF_SYNC_TOS
    #undef _FF_LOAD_TOS
    #undef _FF_TOS
    #undef _FF_NOS
    #undef _FF_SAT
    #undef _FF_PUSH
    #undef _FF_PUSH_PTR
    #undef _FF_PUSH_REAL
    #undef _FF_DROP
    #undef _FF_DROPN
    #undef _FF_SL
    #undef _FF_SO
    #undef _FF_RSL
    #undef _FF_RSO
    #undef _FF_RSL_T
    #undef _FF_RPUSH
    #undef _FF_RKIND
    #undef _FF_COMPILING
    #undef _FF_NEED_DEF
    #undef _FF_CHECK_THROWN
    #undef _FF_CHECK_MEM
    #undef _FF_CHECK_MEM_NEW
    #undef _FF_BAD_SIZE
    #undef _FF_NEED_CAP
    #undef _FF_CHECK_ADDR
    #undef _FF_CHECK_WRITE
    #undef _FF_CHECK_XT
    #undef _FF_CHECK_STR
    #undef _FF_CHECK_FILE
    #undef _FF_CHECK_DATA_WORD
    #undef _FF_RUNNING_DIRECT
}

/**
 * Read one whole line of @p f, however long, into `*buf`, growing it as
 * needed. A fixed buffer would split an overlong line mid-token and feed
 * the halves to ff_eval as two tokens.
 *
 * @param f   Open file.
 * @param buf In/out line buffer (malloc'd; may start NULL).
 * @param cap In/out capacity of `*buf`.
 * @param oom Set when growing the buffer fails.
 * @return false at end of file with nothing read, or on allocation failure.
 */
static bool ff_load_line(FILE *f, char **buf, size_t *cap, bool *oom)
{
    size_t len = 0;
    for (;;)
    {
        if (*cap - len < 2)
        {
            size_t nc = *cap ? *cap * 2 : (size_t)FF_LOAD_LINE_SIZE;
            char *nb = (char *)realloc(*buf, nc);
            if (!nb)
            {
                *oom = true;
                return false;
            }
            *buf = nb;
            *cap = nc;
        }

        size_t room = *cap - len;
        if (room > INT_MAX)
            room = INT_MAX;
        if (!fgets(*buf + len, (int)room, f))
            return len > 0;     /* EOF: a last line without '\n' still counts */

        len += strlen(*buf + len);
        if (len > 0 && (*buf)[len - 1] == '\n')
            return true;
    }
}

/** @copydoc ff_load */
ff_error_t ff_load(ff_t *ff, const char *path)
{
    const bool host = ff_idle(ff);
    if (host)
        ff_host_enter(ff);

    if (!path || !*path)
        return FF_OK;

    /* As in ff_eval: nothing new runs while an exception unwinds. */
    if (ff->state & FF_STATE_THROWN)
        return ff->throw_code == FF_THROW_QUIT ? FF_OK : FF_ERR_CODE(ff->error);

    ff_error_t ec = FF_OK;
    int line_no = 0;
    /* A nested `load` must hand its caller's line count back intact. */
    int prev_line = ff->tokenizer.line;

    /* The whole file is one evaluation as far as the watchdog goes, and
       one exception boundary: a failing line ends the load. */
    ff_eval_enter(ff);

    FILE *f = ff_open_file(ff, path, "r");
    if (!f)
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_FILE_IO,
                  "Failed to open file '%s': %s.", path, strerror(errno));
    else
    {
        char *line = NULL;
        size_t cap = 0;
        bool oom = false;

        /* Each line's ff_eval settles a catchable error itself and
           returns it; an uncatchable one (host abort, QUIT) is still in
           flight when it returns. Either ends the load. */
        while (ff_load_line(f, &line, &cap, &oom))
        {
            ff->tokenizer.line = ++line_no;
            if ((ec = ff_eval(ff, line)) != FF_OK
                    || (ff->state & FF_STATE_THROWN))
                break;
        }
        fclose(f);
        free(line);

        if (ec == FF_OK && !(ff->state & FF_STATE_THROWN))
        {
            if (oom)
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_OOM,
                          "Out of memory reading '%s'.", path);
            /* A runaway comment. The comment state is dropped once
               reported: left set, it would swallow whatever the caller
               evaluates next. */
            else if (ff->tokenizer.state & FF_TOK_STATE_COMMENT)
            {
                ff->tokenizer.state &= ~FF_TOK_STATE_COMMENT;
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_RUN_COMMENT,
                          "Runaway ( comment.");
            }
        }
    }

    /* The load is an evaluation too: a definition the file began and its
       own errors (a runaway comment, say) ended goes with it, as at the
       end of ff_eval. */
    if (ff->state & FF_STATE_THROWN)
    {
        ff_def_unwind(ff);
        ec = ff_settle(ff, host);
    }
    ff_eval_leave(ff);
    ff->tokenizer.line = prev_line;
    ec = FF_ERR_CODE(ec);
    return host ? ff_host_leave(ff, ec) : ec;
}

/* -------------------------------------------------------------------
 * Memory-safety validators. Always compiled — the macros in ff_p.h
 * decide whether to call them. Embedders can also reach for
 * ff_addr_valid() / ff_word_valid() directly from custom native
 * words that take user-supplied addresses, regardless of build mode.
 * ------------------------------------------------------------------- */


/* End of the region — stack, string-arena slab, word heap — containing
   @p a, or NULL; @p writable says whether a program may write it. */
static const char *ff_region_end(const ff_t *ff, const char *a,
                                 bool *writable)
{
    *writable = true;
    if (a == NULL)
        return NULL;

    const char *lo = (const char *)ff->stack.data;
    if (a >= lo && a < lo + sizeof(ff->stack.data))
        return lo + sizeof(ff->stack.data);
    for (const ff_pad_slab_t *sl = ff->pad; sl; sl = sl->next)
        if (a >= sl->data && a < sl->data + sl->used)
            return sl->data + sl->used;
    const ff_region_t *r = ff_dict_region_at(&ff->dict, a);
    if (!r)
        return NULL;
    /* Bytecode is read-only to a program, which could otherwise forge
       what the interpreter follows. */
    *writable = ff_word_holds_data(ff_heap_word(r->owner));
    return (const char *)r->hi;
}

/** @copydoc ff_addr_valid_dict */
bool ff_addr_valid_dict(const ff_t *ff, const void *addr, size_t bytes)
{
    /* The data stack and pad are handled inline by ff_addr_valid; this
       function only covers the dictionary-heaps binary search. The
       NULL/zero/wrap guards are duplicated here so embedders that
       reach this symbol directly still get a safe answer. */
    if (addr == NULL || bytes == 0)
        return false;

    const char *a   = (const char *)addr;
    const char *end = a + bytes;
    if (end < a)
        return false;

    const ff_region_t *r = ff_dict_region_at(&ff->dict, a);
    return r && (uintptr_t)end <= r->hi;
}

/** @copydoc ff_addr_extent */
const char *ff_addr_extent(const ff_t *ff, const void *addr)
{
    bool writable;
    return ff_region_end(ff, (const char *)addr, &writable);
}

/** @copydoc ff_addr_writable */
bool ff_addr_writable(const ff_t *ff, const void *addr, size_t bytes)
{
    const char *a = (const char *)addr;
    bool writable;
    const char *end = ff_region_end(ff, a, &writable);
    return end && writable && bytes && bytes <= (size_t)(end - a);
}

/** @copydoc ff_str_valid */
bool ff_str_valid(const ff_t *ff, const char *s)
{
    const char *end = ff_addr_extent(ff, s);
    return end && memchr(s, '\0', (size_t)(end - s)) != NULL;
}

/** @copydoc ff_word_valid */
bool ff_word_valid(const ff_t *ff, const ff_word_t *w)
{
    if (w == NULL)
        return false;
    /* Shared built-ins live in a contiguous static_pool — fast check
       first. */
    if (ff_dict_is_builtin(&ff->dict, w))
        return true;
    return ff_dict_contains(&ff->dict, w);
}


/** @copydoc ff_abort */
void ff_abort(ff_t *ff)
{
    /* Called from inside a running word (a native), the engine can't be
       torn down under its callers: unwind as the ABORT word does, and the
       reset happens when the exception reaches the outermost call. */
    if (ff->exec_depth > 0 || ff->eval_depth > 0)
    {
        ff_raise(ff, FF_THROW_ABORT, FF_SEV_ERROR | FF_ERR_ABORTED,
                 "Aborted.");
        return;
    }
    ff_reset(ff);
}

/** @copydoc ff_request_abort */
void ff_request_abort(ff_t *ff)
{
    /* Release-store of the abort flag. Signal-handler safe (atomic
       store of an int / sig_atomic_t) and, on a C11-atomics build,
       cross-thread safe — the dispatch loop's matching acquire-load
       picks it up at the next back-branch / word call. On a non-C11
       fallback build the flag is `volatile sig_atomic_t`, which
       still works for the same-thread signal-handler case. */
    if (ff)
        FF_ABORT_STORE(&ff->abort_requested, 1);
}

/** @copydoc ff_banner */
const char *ff_banner(const ff_t *ff)
{
    (void) ff;

    return
"\n"
"       █████   █████\n"
"      ███ ░██ ███ ░██\n"
"     ░███ ░░ ░███ ░░\n"
"    ███████████████\n"
"   ░░░███░ ░░░███░\n"
"     ░███    ░███\n"
"     ░███    ░███\n"
"  ██ ░███ ██ ░███\n"
" ░░█████ ░░█████\n"
"  ░░░░░   ░░░░░\n";
}

/** @copydoc ff_prompt */
const char *ff_prompt(const ff_t *ff)
{
    if ((ff->tokenizer.state & FF_TOK_STATE_COMMENT))
        return "(\xe2\x96\xb6";
    if ((ff->state & FF_STATE_COMPILING))
        return ":\xe2\x96\xb6";
    return "\xe2\x96\xb6";
}

/** @copydoc ff_errno */
ff_error_t ff_errno(const ff_t *ff)
{
    /* Return the bare code so `ff_errno(ff) == FF_ERR_xxx` works. The
       severity bit is kept in ff->error for vtracef; recover it with
       FF_ERR_SEV() if needed. */
    return FF_ERR_CODE(ff->error);
}

/** @copydoc ff_strerror */
const char *ff_strerror(const ff_t *ff)
{
    return ff->error_msg;
}

/** @copydoc ff_err_line */
int ff_err_line(const ff_t *ff)
{
    return ff->error_line;
}

/** @copydoc ff_err_pos */
int ff_err_pos(const ff_t *ff)
{
    return ff->error_pos;
}

/** @copydoc ff_throw_code */
int64_t ff_throw_code(const ff_t *ff)
{
    /* throw_code outlives the exception; the error record says whether
       the last call ended in one. */
    return ff->error == FF_OK ? 0 : (int64_t)ff->throw_code;
}

/** @copydoc ff_printf */
int ff_printf(ff_t *ff, const char *fmt, ...)
{
    if (!ff->platform.vprintf)
        return 0;

    va_list args;
    va_start(args, fmt);
    const int n = ff->platform.vprintf(ff->platform.context, fmt, args);
    va_end(args);

    return n;
}

/**
 * Locate the most recently scanned token for an error report.
 *
 * The tokenizer only records the token's byte offset within ff->input;
 * the line and column are worked out here, on the error path, so the
 * lexer's hot loop doesn't have to track them.
 *
 * @param ff   Engine.
 * @param line Out: 1-based line — the file line under ff_load(), else the
 *             line within the string passed to ff_eval(); 0 when nothing
 *             is being evaluated.
 * @param pos  Out: 0-based byte offset of the token within that line.
 */
static void ff_error_locate(const ff_t *ff, int *line, int *pos)
{
    const ff_tokenizer_t *t = &ff->tokenizer;

    *line = t->line;
    *pos  = 0;
    if (!ff->input)
        return;

    int i = 0, line_start = 0, newlines = 0;
    for (; i < t->pos && ff->input[i]; ++i)
    {
        if (ff->input[i] == '\n')
        {
            ++newlines;
            line_start = i + 1;
        }
    }
    *line = (t->line > 0 ? t->line : 1) + newlines;
    *pos  = i - line_start;
}

/** @copydoc ff_tracef */
ff_error_t ff_tracef(ff_t *ff, ff_error_t e, const char *fmt, ...)
{
    if ((e & FF_SEV_ERROR))
    {
        /* An error is an exception: record it and start unwinding. */
        va_list args;
        va_start(args, fmt);
        ff_raisev(ff, ff_throw_from_error(FF_ERR_CODE(e)), e, fmt, args);
        va_end(args);
    }
    else if (ff->platform.vtracef)
    {
        va_list args;
        va_start(args, fmt);
        ff->platform.vtracef(ff->platform.context, e, fmt, args);
        va_end(args);
    }

    return e;
}
