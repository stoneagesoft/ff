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
    free(ff->pad_buf);

    free(ff);
}

/** @copydoc ff_eval */
ff_error_t ff_eval(ff_t *ff, const char *src)
{
    if (!src
            || !*src)
        return FF_OK;

    const char *prev_input = ff->input;
    int prev_pos = ff->input_pos;
    ff->input = src;
    ff->input_pos = 0;

    ff->state &= ~(FF_STATE_BROKEN | FF_STATE_ERROR);

    /* Watchdog state is per-evaluation: a stale abort-request from a
       previous run is dropped, the opcode counter starts at zero,
       and the polling watchdog will fire after `watchdog_interval`
       opcodes (default 65536). */
    FF_ABORT_CLEAR(&ff->abort_requested);
    ff->opcodes_run      = 0;
    ff->next_watchdog_at = ff->platform.watchdog_interval
                               ? ff->platform.watchdog_interval
                               : 65536;

    int pos = 0;
    ff_dict_t *d = &ff->dict;
    ff_tokenizer_t *t = &ff->tokenizer;
    ff_error_t ec = FF_OK;

    for (;;)
    {
        ff_token_t tok = ff_tokenizer_next(t, src, &pos);

        switch (tok)
        {
            case FF_TOKEN_NULL:
                goto out;

            case FF_TOKEN_WORD:
                if (ff->state & FF_STATE_FORGET_PENDING)
                {
                    ff->state &= ~FF_STATE_FORGET_PENDING;
                    if (!ff_dict_forget(d, t->token))
                    {
                        ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNDEFINED,
                                       "'%s' undefined.", t->token);
                        goto out;
                    }
                }
                else if (ff->state & FF_STATE_TICK_PENDING)
                {
                    ff->state &= ~FF_STATE_TICK_PENDING;
                    const ff_word_t *w = ff_dict_lookup(d, t->token);
                    if (w)
                        ff_stack_push_ptr(&ff->stack, w);
                    else
                    {
                        ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNDEFINED,
                                       "'%s' undefined.", t->token);
                        goto out;
                    }
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
                    /* If a definition is pending, define the token and
                       leave the address of the new word item created for
                       it on the return stack. */
                    ff->state &= ~FF_STATE_DEF_PENDING;
                    if (ff_dict_lookup(d, t->token))
                        ff_tracef(ff, FF_SEV_WARNING | FF_ERR_NON_UNIQUE,
                                  "'%s' isn't unique.", t->token);
                    ff_dict_rename(d, ff_dict_top(d), t->token);
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
                                ff_heap_compile_op(&ff_dict_top(d)->heap, FF_OP_LIT);
                                ff_heap_compile_int(&ff_dict_top(d)->heap,
                                                    (ff_int_t)(intptr_t)w);
                                ff->state &= ~FF_STATE_CTICK_PENDING;
                                ff->state &= ~FF_STATE_CBRACK_PENDING;
                            }
                            else
                            {
                                ff->state &= ~FF_STATE_CBRACK_PENDING;
                                ff_heap_compile_word(&ff_dict_top(d)->heap, w);
                            }
                        }
                        else
                        {
                            ff->input = src;
                            ff->input_pos = pos;
                            if (!ff_exec(ff, w))
                            {
                                ec = FF_ERR_BROKEN;
                                goto out;
                            }
                            if ((ff->state & FF_STATE_ERROR))
                            {
                                ec = ff->error;
                                goto out;
                            }
                            /* Restore --- word may have consumed more input. */
                            pos = ff->input_pos;
                        }
                    }
                    else
                    {
                        ff->state &= ~FF_STATE_COMPILING;
                        ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNDEFINED,
                                       "'%s' undefined.", t->token);
                        goto out;
                    }
                }
                break;

            case FF_TOKEN_INTEGER:
                if (ff->state & FF_STATE_COMPILING)
                    ff_heap_compile_lit(&ff_dict_top(d)->heap, t->integer_val);
                else
                    ff_stack_push(&ff->stack, t->integer_val);
                break;

            case FF_TOKEN_REAL:
                if (ff->state & FF_STATE_COMPILING)
                {
                    ff_heap_compile_op(&ff_dict_top(d)->heap, FF_OP_FLIT);
                    ff_heap_compile_real(&ff_dict_top(d)->heap, t->real_val);
                }
                else
                    ff_stack_push_real(&ff->stack, t->real_val);
                break;

            case FF_TOKEN_STRING:
                if (ff->state & FF_STATE_STRLIT_ANTIC)
                {
                    ff->state &= ~FF_STATE_STRLIT_ANTIC;
                    if (ff->state & FF_STATE_COMPILING)
                        ff_heap_compile_str(&ff_dict_top(d)->heap,
                                            t->token, t->token_len);
                    else
                        ff_printf(ff, "%s", t->token);
                }
                else
                {
                    if (ff->state & FF_STATE_COMPILING)
                    {
                        ff_heap_compile_op(&ff_dict_top(d)->heap, FF_OP_STRLIT);
                        ff_heap_compile_str(&ff_dict_top(d)->heap,
                                            t->token, t->token_len);
                    }
                    else
                    {
                        /* Append the string to the bump arena. The arena
                           grows on demand and is reset only by ff_abort,
                           so the pushed pointer is stable for the engine's
                           lifetime — no silent recycling like the previous
                           ring did. */
                        size_t need = (size_t)t->token_len + 1;
                        if (ff->pad_used + need > ff->pad_size)
                        {
                            size_t nc = ff->pad_size
                                            ? ff->pad_size
                                            : (size_t)FF_PAD_INIT_SIZE;
                            while (nc < ff->pad_used + need)
                                nc *= 2;
                            char *nb = (char *)realloc(ff->pad_buf, nc);
                            if (!nb)
                            {
                                ec = ff_tracef(ff, FF_SEV_ERROR | FF_ERR_OOM,
                                               "Out of memory growing pad arena to %zu bytes.",
                                               nc);
                                goto out;
                            }
                            ff->pad_buf  = nb;
                            ff->pad_size = nc;
                        }
                        char *dst = ff->pad_buf + ff->pad_used;
                        memcpy(dst, t->token, t->token_len);
                        dst[t->token_len] = '\0';
                        ff->pad_used += need;
                        ff_stack_push_ptr(&ff->stack, dst);
                    }
                }
                break;
        }
    }

out:
    /* Single restore-and-return point: every clean exit (FF_TOKEN_NULL)
       and every error path lands here so the input/input_pos snapshot
       is rolled back exactly once. */
    ff->input = prev_input;
    ff->input_pos = prev_pos;
    return ec;
}

/** @copydoc ff_exec */
bool ff_exec(ff_t *ff, ff_word_t *w)
{
    assert(w);

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

    if (ff->state & (FF_STATE_TRACE | FF_STATE_BACKTRACE))
    {
        if (ff->state & FF_STATE_TRACE)
            ff_tracef(ff, FF_SEV_TRACE, "%s \xe2\x86\x92", w->name);
        if (ff->state & FF_STATE_BACKTRACE)
            ff_bt_stack_push(BT, w);
    }

    /* Prepare dispatch.
       - Opcoded built-in  → synthesize a tiny bytecode [opcode, (word_ptr),
         FF_OP_EXIT]. Push a NULL return sentinel on R so EXIT terminates
         cleanly.
       - External FF_OP_NONE word with a fn pointer → call it directly
         (it may, for colon-def'd words via ff_w_nest, set ff->ip).
       - Otherwise nothing to run. */
    ff_int_t exec_scratch[3];
    ff_int_t *ip;

    if (w->opcode != FF_OP_NONE)
    {
        exec_scratch[0] = w->opcode;
        int n = 1;
        if (w->opcode == FF_OP_NEST
                || w->opcode == FF_OP_TNEST
                || w->opcode == FF_OP_DOES_RUNTIME
                || w->opcode == FF_OP_CREATE_RUNTIME
                || w->opcode == FF_OP_CONSTANT_RUNTIME
                || w->opcode == FF_OP_ARRAY_RUNTIME
                || w->opcode == FF_OP_DEFER_RUNTIME)
            exec_scratch[n++] = (ff_int_t)(intptr_t)w;
        exec_scratch[n] = FF_OP_EXIT;
        /* Two-cell return frame: [saved_ip, saved_cur_word]. The
           outermost ip is NULL — the EXIT case detects that and goes
           to `done`. The cur_word slot carries the caller's value so
           a nested ff_exec (e.g. via EXECUTE) restores it cleanly. */
        ff_stack_push(R, 0);
        ff_stack_push(R, (ff_int_t)(intptr_t)prev_cur_word);
        ip = exec_scratch;
    }
    else if (w->flags & FF_WORD_NATIVE)
    {
        ff_word_native_fn(w)(ff);
        ip = ff->ip;
    }
    else
    {
        ip = NULL;
    }

    if (!ip)
        goto done;

    /* Top-of-stack register cache. While dispatching, the topmost data
       stack value lives in `tos` (when S->top > 0); the in-memory slot at
       S->data[S->top - 1] is treated as scratch and may be stale until the
       next _FF_SYNC_TOS / _FF_SYNC. This shaves a load+store off every
       arithmetic operation that takes or returns TOS in place. Pure pushes
       still have to write the displaced TOS back, so the optimization
       targets compute-heavy bytecode rather than push-heavy code. */
    ff_int_t tos = S->top
                        ? S->data[S->top - 1]
                        : 0;

    #define _FF_SYNC_TOS()   do { if (S->top) S->data[S->top - 1] = tos; } while (0)
    #define _FF_LOAD_TOS()   do { if (S->top) tos = S->data[S->top - 1]; } while (0)

    #define _FF_SYNC()    do { ff->ip = ip; _FF_SYNC_TOS(); } while (0)
    #define _FF_RESTORE() do { ip = ff->ip; _FF_LOAD_TOS(); } while (0)

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
       returns bool and must restore state before returning. */
    #define _FF_SL(n) \
        do { \
            if (ff_unlikely((int)S->top < (int)(n))) \
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
       _FF_RSL_T / _FF_RSO_T checks guard an engine-bug-only failure
       and can be elided when FF_R_TRUSTED is on. The unsuffixed
       _FF_RSL / _FF_RSO above stay live because they protect words
       (>R, R>, R@) that user code can stand-alone-misuse. */
#if FF_R_TRUSTED
    #define _FF_RSL_T(n)  ((void)0)
    #define _FF_RSO_T(n)  ((void)0)
#else
    #define _FF_RSL_T(n)  _FF_RSL(n)
    #define _FF_RSO_T(n)  _FF_RSO(n)
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
                if (!ip)
                    goto done;
                _FF_NEXT();

            /* Built-in word bodies live in per-category headers that
               are included here so each case is inline. The headers
               reference the macros (_FF_NEXT, _FF_SYNC, _FF_RESTORE, _FF_SO,
               _FF_RSO), labels (done, broken), and local variables
               (S, R, BT, ip, ff, nest_code, bt_size) in this scope. */
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
            #include "ff_words_array_p.h"
            #include "ff_words_dict_p.h"

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
                goto broken;
#else
                FF_UNREACHABLE();
#endif
        }
    }


    /* --- Exit points --- */

_watchdog_abort:
    /* Watchdog or async ff_request_abort fired. Surface it as a
       FF_SEV_ERROR | FF_ERR_ABORTED, clear the flag (so the next
       ff_eval call starts fresh), and join the broken-state cleanup
       path. */
    _FF_SYNC();
    ff_tracef(ff, FF_SEV_ERROR | FF_ERR_ABORTED,
              "Aborted after %llu opcodes.",
              (unsigned long long)ff->opcodes_run);
    FF_ABORT_CLEAR(&ff->abort_requested);
    ff->state |= FF_STATE_BROKEN;
    goto broken;

broken:
    /* Flush the local watchdog batch counter back into the engine's
       running total before returning, so a host that reads
       ff->opcodes_run after a failed run sees an accurate count. */
    ff->opcodes_run += (uint64_t)(FF_WD_BATCH - wd_tick);
    ff->ip = ip;
    if (S->top) S->data[S->top - 1] = tos;
    ff->cur_word = prev_cur_word;
    BT->top = bt_size;
    return false;

done:
    ff->opcodes_run += (uint64_t)(FF_WD_BATCH - wd_tick);
    ff->ip = ip;
    if (S->top) S->data[S->top - 1] = tos;
    ff->cur_word = prev_cur_word;
    BT->top = bt_size;
    return true;

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
    #undef _FF_RSO_T
    #undef _FF_COMPILING
}

/** @copydoc ff_load */
ff_error_t ff_load(ff_t *ff, const char *path)
{
    if (!path || !*path)
        return FF_OK;

    FILE *f = fopen(path, "r");
    if (!f)
        return ff_tracef(ff, FF_SEV_ERROR | FF_ERR_FILE_IO,
                         "Failed to open file '%s': %s.",
                         path, strerror(errno));

    ff_error_t ec = FF_OK;
    int line_no = 0;
    ff_int_t *prev_ip = ff->ip;

    char line[FF_LOAD_LINE_SIZE];
    while (fgets(line, sizeof(line), f))
    {
        ff->tokenizer.line = line_no++;
        if ((ec = ff_eval(ff, line)) != FF_OK)
            break;
    }
    fclose(f);

    ff->ip = prev_ip;
    ff->tokenizer.line = 0;

    /* If there were no other errors, check for a runaway comment. */
    if (ec == FF_OK
            && (ff->tokenizer.state & FF_TOK_STATE_COMMENT))
        return ff_tracef(ff, FF_SEV_ERROR | FF_ERR_RUN_COMMENT, "Runaway ( comment.");

    return ec;
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
    ff->state |= FF_STATE_ABORTED;
    ff->stack.top = 0;
    ff->r_stack.top = 0;
    ff->ip = NULL;
    ff->state = 0;
    ff->tokenizer.state = 0;
    ff->cur_word = NULL;
    /* Reset the transient-string arena. Anything still on the data
       stack pointing into the pad becomes garbage — but we just
       cleared the data stack, so there's nothing to dangle. */
    ff->pad_used = 0;
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
    return ff->error;
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

/** @copydoc ff_tracef */
ff_error_t ff_tracef(ff_t *ff, ff_error_t e, const char *fmt, ...)
{
    if ((e & FF_SEV_ERROR))
    {
        ff->error = e;
        ff->error_line = ff->tokenizer.line;
        ff->error_pos = ff->tokenizer.pos;

        ff->state |= FF_STATE_ERROR;

        va_list args;
        va_start(args, fmt);
        vsnprintf(ff->error_msg, sizeof(ff->error_msg), fmt, args);
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
