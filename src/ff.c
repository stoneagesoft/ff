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
       singleton — initialised lazily here on the first ff_new call.
       Embedders that fan out engines across threads should warm the
       singleton from the main thread first; see ff_builtins_default(). */
    ff_dict_init(&ff->dict, ff_builtins_default());
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
void ff_warmup(void)
{
    (void)ff_builtins_default();
}

/** @copydoc ff_register */
ff_error_t ff_register(ff_t *ff, const ff_native_word_t *words)
{
    for (const ff_native_word_t *w = words; w && w->name; ++w)
        ff_dict_append(&ff->dict,
                       w->immediate
                           ? ff_im_word_new(w->name, w->fn, FF_OP_NONE, w->manual)
                           : ff_word_new(w->name, w->fn, FF_OP_NONE, w->manual));
    return FF_OK;
}

/** @copydoc ff_depth */
size_t ff_depth(const ff_t *ff)
{
    return ff->stack.top;
}

/** @copydoc ff_push_int */
bool ff_push_int(ff_t *ff, int64_t v)
{
    if (ff->stack.top >= FF_STACK_SIZE)
        return false;
    ff_stack_push(&ff->stack, (ff_int_t)v);
    return true;
}

/** @copydoc ff_pop_int */
bool ff_pop_int(ff_t *ff, int64_t *out)
{
    if (ff->stack.top == 0)
        return false;
    ff_int_t v = ff_stack_pop(&ff->stack);
    if (out)
        *out = (int64_t)v;
    return true;
}

/** @copydoc ff_push_real */
bool ff_push_real(ff_t *ff, double v)
{
    if (ff->stack.top >= FF_STACK_SIZE)
        return false;
    ff_stack_push_real(&ff->stack, (ff_real_t)v);
    return true;
}

/** @copydoc ff_pop_real */
bool ff_pop_real(ff_t *ff, double *out)
{
    if (ff->stack.top == 0)
        return false;
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
        size_t cap = need > (size_t)FF_PAD_INIT_SIZE
                         ? need : (size_t)FF_PAD_INIT_SIZE;
        ff_pad_slab_t *ns = (ff_pad_slab_t *)malloc(sizeof(*ns) + cap);
        if (!ns)
            return NULL;
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
 * Enter an evaluation (ff_eval or ff_load).
 *
 * The watchdog state belongs to the outermost evaluation. Resetting it on
 * a nested entry (`evaluate`, `load`, or a native word evaluating source
 * inside a host's ff_exec) would restart the opcode budget and drop a
 * pending ff_request_abort() on every pass through a loop around them,
 * so untrusted code could never be stopped.
 *
 * @param ff Engine.
 */
static void ff_eval_enter(ff_t *ff)
{
    if (ff->eval_depth++ > 0 || ff->exec_depth > 0)
        return;

    FF_ABORT_CLEAR(&ff->abort_requested);
    ff->opcodes_run      = 0;
    ff->next_watchdog_at = ff->platform.watchdog_interval
                               ? ff->platform.watchdog_interval
                               : 65536;
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
        case FF_ERR_SCOPE_RSTACK: return FF_THROW_RSTACK_IMBAL;
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
        case FF_THROW_NESTING:       return FF_ERR_MALFORMED;
        case FF_THROW_RSTACK_IMBAL:  return FF_ERR_SCOPE_RSTACK;
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

/**
 * Create the word that a defining word (`:`, `create`, `variable`, …)
 * makes; the next token names it.
 *
 * @param ff Engine.
 * @param op Opcode of the new word.
 * @return The new word, already in the dictionary.
 */
static ff_word_t *ff_def_new(ff_t *ff, ff_opcode_t op)
{
    ff_word_t *w = ff_dict_append(&ff->dict, ff_word_new(" ", NULL, op, NULL));
    ff->unnamed = w;
    ff->state |= FF_STATE_DEF_PENDING;
    return w;
}

/**
 * Begin a colon definition (`:`).
 *
 * @param ff Engine.
 */
static void ff_def_begin(ff_t *ff)
{
    ff->compiling = ff_def_new(ff, FF_OP_NONE);
    ff->def_depth = ff->eval_depth;
    ff->n_cf = 0;
    ff->state |= FF_STATE_COMPILING;
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
    {
        if (ff->unnamed == w)
            ff->unnamed = NULL;
        ff_dict_remove(&ff->dict, w);
    }
}

/**
 * Remove the word that a defining word made if its name never came.
 *
 * @param ff Engine.
 */
static void ff_def_drop_unnamed(ff_t *ff)
{
    if ((ff->state & FF_STATE_DEF_PENDING) && ff->unnamed
            && ff->unnamed != ff->compiling)
        ff_dict_remove(&ff->dict, ff->unnamed);
    ff->unnamed = NULL;
    ff->state &= ~FF_STATE_DEF_PENDING;
}

/**
 * Clean up the compiler after an exception that unwinds out of the
 * evaluation at depth ff->eval_depth.
 *
 * The rest of that input is discarded, so the one-shot next-token flags
 * go, and so does a word still waiting for its name. A definition begun
 * at this depth or deeper is abandoned. One begun further out stays open:
 * the exception is settled before it gets there — an `evaluate` run from
 * `[ ]` pushes the code, and compilation carries on.
 *
 * @param ff Engine.
 */
static void ff_def_unwind(ff_t *ff)
{
    ff_def_drop_unnamed(ff);
    ff->state &= ~FF_STATE_PENDING_ALL;
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
 * @param ff Engine.
 * @param w  Word to call.
 * @return false, with the error raised, if @p w can't be compiled here.
 */
static bool ff_compile_call(ff_t *ff, const ff_word_t *w)
{
    ff_heap_t *h = &ff->compiling->heap;

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
    return true;
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
    /* Before the state flags go: they say whether a word still waits for
       its name. ff_def_abandon also frees the `{` scope records, whose
       names ff_scope_arg would otherwise go on resolving. */
    ff_def_drop_unnamed(ff);
    ff_def_abandon(ff);

    ff->stack.top = 0;
    ff->r_stack.top = 0;
    ff->ip = NULL;
    ff->state = 0;
    ff->tokenizer.state = 0;
    ff->cur_word = NULL;

    ff->n_scopes = 0;
    ff->stack.floor = 0;
    /* Reset the transient-string arena by freeing all slabs. Anything
       still pointing into the pad becomes garbage — but we just cleared
       the data and return stacks, so there is nothing to dangle. */
    for (ff_pad_slab_t *sl = ff->pad; sl; )
    {
        ff_pad_slab_t *next = sl->next;
        free(sl);
        sl = next;
    }
    ff->pad = NULL;
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

    /* Watchdog state is per outermost evaluation: a stale abort request
       from a previous run is dropped and the opcode count starts at zero. */
    ff_eval_enter(ff);

    int pos = 0;
    ff_dict_t *d = &ff->dict;
    ff_tokenizer_t *t = &ff->tokenizer;
    ff_error_t ec = FF_OK;

    for (;;)
    {
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
           exactly as a `:` name or an open `(` comment may. ffsh feeds
           one line per call, so anything else would make a multi-line
           signature work in a loaded file but not at the prompt. */
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

        /* One-shot pending flags consume the very next token. Validate its
           kind here, before the kind switch, so a wrong-kind token errors
           (and the flag is cleared at `out:` by ff_def_unwind) instead of
           being processed by the INTEGER/REAL/STRING case while the flag
           silently survives to ambush a later token — e.g. `: 42`, `' 5`,
           `." 42`. NULL passes through so a flag may span ff_eval calls
           (ffsh feeds one line per call). */
        if (tok != FF_TOKEN_NULL)
        {
            if ((ff->state & FF_STATE_NAME_PENDING) && tok != FF_TOKEN_WORD)
            {
                ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_MISSING,
                               "Expected a word name, got '%s'.", t->token);
                goto out;
            }
            if ((ff->state & FF_STATE_STRLIT_ANTIC) && tok != FF_TOKEN_STRING)
            {
                ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_MISSING,
                               "Expected a string literal, got '%s'.", t->token);
                goto out;
            }
        }

        switch (tok)
        {
            case FF_TOKEN_NULL:
                goto out;

            case FF_TOKEN_WORD:
                if (ff->state & FF_STATE_FORGET_PENDING)
                {
                    ff->state &= ~FF_STATE_FORGET_PENDING;
                    /* Forgetting cuts off every later word, which would
                       free the open definition under the compiler. */
                    if (ff->compiling)
                    {
                        ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_FORGET_PROT,
                                       "Can't forget while '%s' is being defined.",
                                       ff->compiling->name);
                        goto out;
                    }
                    if (!ff_dict_forget(d, t->token))
                    {
                        ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNDEFINED,
                                       "'%s' undefined.", t->token);
                        goto out;
                    }
                }
                else if (ff->state & (FF_STATE_POSTPONE_PENDING
                                          | FF_STATE_COMPILE_PENDING))
                {
                    /* `postpone name`. Append name's compilation semantics
                       to the current definition: for an immediate word,
                       compile a call so it runs when this definition runs;
                       for a non-immediate word, emit POSTPONE_RUNTIME so
                       that a call to name is *compiled* when this definition
                       runs (deferred by one level). `compile name` defers
                       any word that way, immediate or not. */
                    bool defer_all = (ff->state & FF_STATE_COMPILE_PENDING) != 0;
                    ff->state &= ~(FF_STATE_POSTPONE_PENDING
                                   | FF_STATE_COMPILE_PENDING);
                    ff_word_t *w = ff_dict_lookup(d, t->token);
                    if (!w)
                    {
                        ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNDEFINED,
                                       "'%s' undefined.", t->token);
                        goto out;
                    }
                    if (!(ff->state & FF_STATE_COMPILING))
                    {
                        ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_NOT_IN_DEF,
                                       "%s outside a definition.",
                                       defer_all ? "compile" : "postpone");
                        goto out;
                    }
                    ff_heap_t *ph = &ff->compiling->heap;
                    if ((w->flags & FF_WORD_IMMEDIATE) && !defer_all)
                        ff_heap_compile_word(ph, w);
                    else
                    {
                        ff_heap_compile_op(ph, FF_OP_POSTPONE_RUNTIME);
                        ff_heap_compile_int(ph, (ff_int_t)(intptr_t)w);
                    }
                }
                else if (ff->state & FF_STATE_TICK_PENDING)
                {
                    ff->state &= ~FF_STATE_TICK_PENDING;
                    const ff_word_t *w = ff_dict_lookup(d, t->token);
                    if (!w)
                    {
                        ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNDEFINED,
                                       "'%s' undefined.", t->token);
                        goto out;
                    }
                    if (!ff_eval_room(ff, &ec))
                        goto out;
                    ff_stack_push_ptr(&ff->stack, w);
                }
                else if (ff->state & FF_STATE_IS_PENDING)
                {
                    /* `is name` (ANS 6.2.1830). The xt is already on TOS
                       from a preceding `'`; this token names the deferred
                       word that should receive it. The deferred word's
                       slot lives at heap.data[0]. */
                    ff->state &= ~FF_STATE_IS_PENDING;
                    ff_word_t *w = ff_dict_lookup(d, t->token);
                    if (!w)
                    {
                        ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNDEFINED,
                                       "'%s' undefined.", t->token);
                        goto out;
                    }
                    if (w->opcode != FF_OP_DEFER_RUNTIME)
                    {
                        ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNSUPPORTED,
                                       "'%s' is not a deferred word.", t->token);
                        goto out;
                    }
                    if (ff->stack.top < 1)
                    {
                        ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_STACK_UNDER,
                                       "Stack underflow: 'is' expected an xt.");
                        goto out;
                    }
                    w->heap.data[0] = ff_stack_pop(&ff->stack);
                }
                else if (ff->state & FF_STATE_DEF_PENDING)
                {
                    /* The name of the word a defining word just made. */
                    ff->state &= ~FF_STATE_DEF_PENDING;
                    if (ff_dict_lookup(d, t->token))
                        ff_tracef(ff, FF_SEV_WARNING | FF_ERR_NON_UNIQUE,
                                  "'%s' isn't unique.", t->token);
                    if (ff->unnamed)
                        ff_dict_rename(d, ff->unnamed, t->token);
                    ff->unnamed = NULL;
                }
                else if ((ff->state & FF_STATE_COMPILING)
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
                    if (w)
                    {
                        /* Test the state. If we're interpreting, execute
                           the word in all cases.  If we're compiling,
                           compile the word unless it is a compiler word
                           flagged for immediate execution. */
                        if ((ff->state & FF_STATE_COMPILING)
                                && ((ff->state & FF_STATE_CBRACK_PENDING)
                                        || (ff->state & FF_STATE_CTICK_PENDING)
                                        || !(w->flags & FF_WORD_IMMEDIATE)))
                        {
                            if (ff->state & FF_STATE_CTICK_PENDING)
                            {
                                /* If a compile-time tick preceded this
                                   word, compile a (lit) word to cause its
                                   address to be pushed at execution time. */
                                ff_heap_compile_op(&ff->compiling->heap, FF_OP_LIT);
                                ff_heap_compile_int(&ff->compiling->heap,
                                                    (ff_int_t)(intptr_t)w);
                                ff->state &= ~FF_STATE_CTICK_PENDING;
                                ff->state &= ~FF_STATE_CBRACK_PENDING;
                            }
                            else
                            {
                                ff->state &= ~FF_STATE_CBRACK_PENDING;
                                if (!ff_compile_call(ff, w))
                                    goto out;
                            }
                        }
                        else
                        {
                            ff->input = src;
                            ff->input_pos = pos;
                            /* An exception — error, THROW, ABORT, QUIT —
                               discards the rest of the input: `out`
                               settles it into the return code. */
                            if (!ff_exec(ff, w))
                                goto out;
                            /* Restore --- word may have consumed more input. */
                            pos = ff->input_pos;
                        }
                    }
                    else
                    {
                        ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNDEFINED,
                                       "'%s' undefined.", t->token);
                        goto out;
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
                if (ff->state & FF_STATE_STRLIT_ANTIC)
                {
                    /* The string `."`, `.(` or `abort"` is waiting for:
                       what to do with it was fixed when that word ran, not
                       by the state now — `.(` prints at once even in a
                       definition. */
                    ff->state &= ~FF_STATE_STRLIT_ANTIC;
                    if (ff->strlit_compile)
                    {
                        if (!ff->compiling)
                        {
                            ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_NOT_IN_DEF,
                                           "No definition to compile the string into.");
                            goto out;
                        }
                        ff_heap_compile_op(&ff->compiling->heap, ff->strlit_op);
                        ff_heap_compile_str(&ff->compiling->heap,
                                            t->token, t->token_len);
                    }
                    else if (ff->strlit_op == FF_OP_ABORTQ_RUNTIME)
                    {
                        ff_raise(ff, FF_THROW_ABORTQ,
                                 FF_SEV_ERROR | FF_ERR_ABORTED, "%s", t->token);
                        goto out;
                    }
                    else
                        ff_printf(ff, "%s", t->token);
                }
                else
                {
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
                            ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_OOM,
                                           "Out of memory growing pad arena.");
                            goto out;
                        }
                        ff_stack_push_ptr(&ff->stack, dst);
                    }
                }
                break;
        }
    }

out:
    /* An exception discards the rest of this input, and with it whatever
       was being compiled from it: pending next-token flags, a word still
       waiting for its name, and a definition begun at this depth — with
       its scope and control-flow records. On the clean-exit path they all
       carry over: a definition, an open `{` or a pending name may continue
       in the next ff_eval call. */
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
    ec = ff_settle(ff, ff->eval_depth == 1 && ff->exec_depth == 0);
    ff_eval_leave(ff);
    return ec;
}

/** @copydoc ff_exec */
bool ff_exec(ff_t *ff, ff_word_t *w)
{
    assert(w);

    /* Nothing new starts while an exception unwinds (a native word that
       raised an error and then called back in). */
    if (ff->state & FF_STATE_THROWN)
        return false;

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
    if (ff_unlikely(R->top + 2 > FF_STACK_SIZE))
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_RSTACK_OVER,
                  "Return stack overflow: %d item(s) would not fit.", 2);
    else
    {
        ff_stack_push(R, 0);
        ff_stack_push(R, (ff_int_t)(intptr_t)prev_cur_word);
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
#else
    #define _FF_CHECK_ADDR(addr, bytes) ((void)0)
    #define _FF_CHECK_XT(w)             ((void)0)
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
       user-reachable failure, never an engine bug. */
#if FF_R_TRUSTED
    #define _FF_RSL_T(n)  ((void)0)
#else
    #define _FF_RSL_T(n)  _FF_RSL(n)
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
                if (!ip)
                    goto done;
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
    ff->ip = ip;
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
    if (!ok && ff->exec_depth == 0 && ff->eval_depth == 0)
    {
        /* A definition this call began goes with it. One the host is
           feeding in through ff_eval() calls stays open: this call is not
           part of that input. */
        if (ff->compiling && ff->def_depth == 0)
            ff_def_abandon(ff);
        (void)ff_settle(ff, true);
    }
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
    #undef _FF_COMPILING
    #undef _FF_NEED_DEF
    #undef _FF_CHECK_THROWN
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
    if (!path || !*path)
        return FF_OK;

    /* As in ff_eval: nothing new runs while an exception unwinds. */
    if (ff->state & FF_STATE_THROWN)
        return ff->throw_code == FF_THROW_QUIT ? FF_OK : FF_ERR_CODE(ff->error);

    ff_error_t ec = FF_OK;
    int line_no = 0;
    /* A nested `load` must hand its caller's line count back intact. */
    int prev_line = ff->tokenizer.line;
    ff_int_t *prev_ip = ff->ip;

    /* The whole file is one evaluation as far as the watchdog goes, and
       one exception boundary: a failing line ends the load. */
    ff_eval_enter(ff);

    FILE *f = fopen(path, "r");
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
        ec = ff_settle(ff, ff->eval_depth == 1 && ff->exec_depth == 0);
    }
    ff_eval_leave(ff);
    ff->ip = prev_ip;
    ff->tokenizer.line = prev_line;
    return FF_ERR_CODE(ec);
}

/* -------------------------------------------------------------------
 * Memory-safety validators. Always compiled — the macros in ff_p.h
 * decide whether to call them. Embedders can also reach for
 * ff_addr_valid() / ff_word_valid() directly from custom native
 * words that take user-supplied addresses, regardless of build mode.
 * ------------------------------------------------------------------- */

/** @copydoc ff_addr_valid_dict */
bool ff_addr_valid_dict(const ff_t *ff, const void *addr, size_t bytes)
{
    /* Stacks and pad are handled inline by ff_addr_valid; this
       function only covers the dictionary-heaps binary search. The
       NULL/zero/wrap guards are duplicated here so embedders that
       reach this symbol directly still get a safe answer. */
    if (addr == NULL || bytes == 0)
        return false;

    const char *a   = (const char *)addr;
    const char *end = a + bytes;
    if (end < a)
        return false;

    /* Two indexes — the per-instance one for user-word heaps
       (rebuilt lazily on the first call after a mutation), and the
       shared one for built-in native fn-pointer heaps (built once
       during ff_builtins_init and immutable). Membership in either
       is enough. */
    size_t n = 0;
    const ff_interval_t *ivs = ff_dict_intervals((ff_dict_t *)&ff->dict, &n);
    for (int pass = 0; pass < 2; ++pass)
    {
        if (n > 0)
        {
            size_t lo_i = 0, hi_i = n;
            while (lo_i < hi_i)
            {
                size_t mid = lo_i + (hi_i - lo_i) / 2;
                if (ivs[mid].lo <= a)
                    lo_i = mid + 1;
                else
                    hi_i = mid;
            }
            if (lo_i > 0)
            {
                const ff_interval_t *iv = &ivs[lo_i - 1];
                if (a >= iv->lo && end <= iv->hi)
                    return true;
            }
        }
        if (pass == 0 && ff->dict.builtins)
        {
            ivs = ff->dict.builtins->intervals;
            n   = ff->dict.builtins->intervals_count;
        }
        else
        {
            break;
        }
    }
    return false;
}

/** @copydoc ff_word_valid */
bool ff_word_valid(const ff_t *ff, const ff_word_t *w)
{
    if (w == NULL)
        return false;
    /* Shared built-ins live in a contiguous static_pool — fast range
       check first. */
    const ff_builtins_t *b = ff->dict.builtins;
    if (b && w >= b->static_pool && w < b->static_pool + b->static_pool_size)
        return true;
    /* User words: linear scan over the per-instance words array. */
    for (size_t i = 0; i < ff->dict.count; ++i)
        if (ff->dict.words[i] == w)
            return true;
    return false;
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
