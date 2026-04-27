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
 * the @c _PUSH / @c _DROP / @c _NOS / @c _SAT macros and the
 * @c _SYNC_TOS / @c _LOAD_TOS hooks woven into @c _FF_SYNC and
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


// Public

/** @copydoc ff_new */
ff_t *ff_new(const ff_platform_t *p)
{
    assert(p);

    ff_t *ff = (ff_t *)calloc(1, sizeof(ff_t));

    ff->platform = *p;

    ff->base = FF_BASE_DEC;

    ff_dict_init(&ff->dict);
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
    ff->abort_requested  = 0;
    ff->opcodes_run      = 0;
    ff->next_watchdog_at = ff->platform.watchdog_interval
                               ? ff->platform.watchdog_interval
                               : 65536;

    int pos = 0;
    ff_dict_t *d = &ff->dict;
    ff_tokenizer_t *t = &ff->tokenizer;

    for (;;)
    {
        ff_token_t tok = ff_tokenizer_next(t, src, &pos);

        switch (tok)
        {
            case FF_TOKEN_NULL:
                ff->input = prev_input;
                ff->input_pos = prev_pos;
                return FF_OK;

            case FF_TOKEN_WORD:
                if (ff->state & FF_STATE_FORGET_PENDING)
                {
                    ff->state &= ~FF_STATE_FORGET_PENDING;
                    if (!ff_dict_forget(d, t->token))
                    {
                        ff->input = prev_input;
                        ff->input_pos = prev_pos;
                        return ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNDEFINED,
                                         "'%s' undefined.", t->token);
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
                        ff->input = prev_input;
                        ff->input_pos = prev_pos;
                        return ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNDEFINED,
                                         "'%s' undefined.", t->token);
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
                        ff->input = prev_input;
                        ff->input_pos = prev_pos;
                        return ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNDEFINED,
                                         "'%s' undefined.", t->token);
                    }
                    if (w->opcode != FF_OP_DEFER_RUNTIME)
                    {
                        ff->input = prev_input;
                        ff->input_pos = prev_pos;
                        return ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNSUPPORTED,
                                         "'%s' is not a deferred word.", t->token);
                    }
                    if (ff->stack.top < 1)
                    {
                        ff->input = prev_input;
                        ff->input_pos = prev_pos;
                        return ff_tracef(ff, FF_SEV_ERROR | FF_ERR_STACK_UNDER,
                                         "Stack underflow: 'is' expected an xt.");
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
                                ff->input = prev_input;
                                ff->input_pos = prev_pos;
                                return FF_ERR_BROKEN;
                            }
                            if ((ff->state & FF_STATE_ERROR))
                            {
                                ff->input = prev_input;
                                ff->input_pos = prev_pos;
                                return ff->error;
                            }
                            /* Restore --- word may have consumed more input. */
                            pos = ff->input_pos;
                        }
                    }
                    else
                    {
                        ff->state &= ~FF_STATE_COMPILING;
                        ff->input = prev_input;
                        ff->input_pos = prev_pos;
                        return ff_tracef(ff, FF_SEV_ERROR | FF_ERR_UNDEFINED,
                                         "'%s' undefined.", t->token);
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
                        /* Temporary string in pad. */
                        int pi = ff->pad_i;
                        int len = t->token_len < FF_PAD_SIZE - 1
                                        ? t->token_len
                                        : FF_PAD_SIZE - 1;
                        memcpy(ff->pad[pi], t->token, len);
                        ff->pad[pi][len] = '\0';
                        ff_stack_push_ptr(&ff->stack, ff->pad[pi]);
                        ff->pad_i = (pi + 1) % FF_PAD_COUNT;
                    }
                }
                break;
        }
    }
}

/** @copydoc ff_exec */
bool ff_exec(ff_t *ff, ff_word_t *w)
{
    assert(w);

    ff_stack_t *S = &ff->stack;
    ff_stack_t *R = &ff->r_stack;
    ff_bt_stack_t *BT = &ff->bt_stack;

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
        ff_stack_push(R, 0);   /* NULL return sentinel */
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
       next _SYNC_TOS / _FF_SYNC. This shaves a load+store off every
       arithmetic operation that takes or returns TOS in place. Pure pushes
       still have to write the displaced TOS back, so the optimization
       targets compute-heavy bytecode rather than push-heavy code. */
    ff_int_t tos = S->top
                        ? S->data[S->top - 1]
                        : 0;

    #define _SYNC_TOS()   do { if (S->top) S->data[S->top - 1] = tos; } while (0)
    #define _LOAD_TOS()   do { if (S->top) tos = S->data[S->top - 1]; } while (0)

    #define _FF_SYNC()    do { ff->ip = ip; _SYNC_TOS(); } while (0)
    #define _FF_RESTORE() do { ip = ff->ip; _LOAD_TOS(); } while (0)

    /* In-register convenience accessors used by case bodies. _TOS is the
       cached TOS value (lvalue), _NOS / _SAT(i) reach into memory below
       the cache. _SAT(0) is invalid — use _TOS for index 0. */
    #define _TOS          tos
    #define _NOS          (S->data[S->top - 2])
    #define _SAT(i)       (S->data[S->top - 1 - (i)])

    /* Stack-mutating helpers used by case bodies. _PUSH stores the
       displaced TOS to memory before bringing in the new top; _DROP /
       _DROPN reload TOS from memory if any items remain. */
    #define _PUSH(x) \
        do { \
            if (S->top) S->data[S->top - 1] = tos; \
            tos = (x); \
            ++S->top; \
        } while (0)
    #define _PUSH_PTR(p)  _PUSH((ff_int_t)(intptr_t)(p))
    #define _PUSH_REAL(r) \
        do { \
            if (S->top) S->data[S->top - 1] = tos; \
            ff_set_real(&tos, (r)); \
            ++S->top; \
        } while (0)
    #define _DROP() \
        do { \
            if (--S->top) tos = S->data[S->top - 1]; \
        } while (0)
    #define _DROPN(n) \
        do { \
            S->top -= (n); \
            if (S->top) tos = S->data[S->top - 1]; \
        } while (0)

    /* Dispatch-context validation. Unlike FF_SL/FF_SO/FF_RSL/FF_RSO in
       ff_p.h these `goto done` rather than `return`-ing, because ff_exec
       returns bool and must restore state before returning. */
    #define _FF_SL(n) \
        do { \
            if ((int)S->top < (int)(n)) \
            { \
                _FF_SYNC(); \
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_STACK_UNDER, \
                          "Stack underflow: %d item(s) expected.", (int)(n)); \
                goto done; \
            } \
        } while (0)
    #define _FF_SO(n) \
        do { \
            if ((int)S->top + (int)(n) > FF_STACK_SIZE) \
            { \
                _FF_SYNC(); \
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_STACK_OVER, \
                          "Stack overflow: %d item(s) would not fit.", (int)(n)); \
                goto done; \
            } \
        } while (0)
    #define _FF_RSL(n) \
        do { \
            if ((int)R->top < (int)(n)) \
            { \
                _FF_SYNC(); \
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_RSTACK_UNDER, \
                          "Return stack underflow: %d item(s) expected.", (int)(n)); \
                goto done; \
            } \
        } while (0)
    #define _FF_RSO(n) \
        do { \
            if ((int)R->top + (int)(n) > FF_STACK_SIZE) \
            { \
                _FF_SYNC(); \
                ff_tracef(ff, FF_SEV_ERROR | FF_ERR_RSTACK_OVER, \
                          "Return stack overflow: %d item(s) would not fit.", (int)(n)); \
                goto done; \
            } \
        } while (0)
    #define _FF_COMPILING \
        do { \
            if (!(ff->state & FF_STATE_COMPILING)) \
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
            if (!ff_addr_valid(ff, (addr), (size_t)(bytes))) \
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
            if (!ff_word_valid(ff, (w))) \
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

    /* Watchdog: bump the opcode counter, check for an async abort
       request, and (every N opcodes) call the host's polling
       watchdog. Wired into the back-branch and word-call sites so
       the cost is paid at most once per loop iteration / nested
       call, not per opcode. */
    #define _FF_WATCHDOG_TICK() \
        do { \
            ff->opcodes_run++; \
            if (ff->abort_requested) \
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
        } while (0)

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
                FF_UNREACHABLE();
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
    ff->abort_requested = 0;
    ff->state |= FF_STATE_BROKEN;
    goto broken;

broken:
    ff->ip = ip;
    if (S->top) S->data[S->top - 1] = tos;
    ff->cur_word = NULL;
    BT->top = bt_size;
    return false;

done:
    ff->ip = ip;
    if (S->top) S->data[S->top - 1] = tos;
    ff->cur_word = NULL;
    BT->top = bt_size;
    return true;

    #undef _FF_NEXT
    #undef _FF_SYNC
    #undef _FF_RESTORE
    #undef _SYNC_TOS
    #undef _LOAD_TOS
    #undef _TOS
    #undef _NOS
    #undef _SAT
    #undef _PUSH
    #undef _PUSH_PTR
    #undef _PUSH_REAL
    #undef _DROP
    #undef _DROPN
    #undef _FF_SL
    #undef _FF_SO
    #undef _FF_RSL
    #undef _FF_RSO
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

/** @copydoc ff_addr_valid */
bool ff_addr_valid(const ff_t *ff, const void *addr, size_t bytes)
{
    if (addr == NULL || bytes == 0)
        return false;

    const char *a   = (const char *)addr;
    const char *end = a + bytes;
    /* Pointer wrap (a + bytes overflowed) — always invalid. */
    if (end < a)
        return false;

    /* Data stack. */
    const char *s_lo = (const char *)ff->stack.data;
    const char *s_hi = s_lo + sizeof(ff->stack.data);
    if (a >= s_lo && end <= s_hi)
        return true;

    /* Return stack. */
    const char *r_lo = (const char *)ff->r_stack.data;
    const char *r_hi = r_lo + sizeof(ff->r_stack.data);
    if (a >= r_lo && end <= r_hi)
        return true;

    /* Pad ring. */
    const char *p_lo = (const char *)ff->pad;
    const char *p_hi = p_lo + sizeof(ff->pad);
    if (a >= p_lo && end <= p_hi)
        return true;

    /* Any dictionary word's heap. Walked linearly — see the comment
       on FF_SAFE_MEM in ff_config_p.h about the cost trade-off. */
    for (size_t i = 0; i < ff->dict.count; ++i)
    {
        const ff_word_t *w = ff->dict.words[i];
        if (w == NULL)
            continue;
        const ff_heap_t *h = &w->heap;
        if (h->data == NULL || h->capacity == 0)
            continue;
        const char *h_lo = (const char *)h->data;
        const char *h_hi = h_lo + h->capacity * sizeof(ff_int_t);
        if (a >= h_lo && end <= h_hi)
            return true;
    }

    return false;
}

/** @copydoc ff_word_valid */
bool ff_word_valid(const ff_t *ff, const ff_word_t *w)
{
    if (w == NULL)
        return false;
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
}

/** @copydoc ff_request_abort */
void ff_request_abort(ff_t *ff)
{
    /* Atomic store of a sig_atomic_t — safe from a signal handler
       and (with sig_atomic_t's "lock-free for at least 1 byte"
       guarantee on every C17 platform) safe from another thread on
       the lock-free architectures we ship on. The dispatch loop
       picks it up at the next back-branch / word call. */
    if (ff)
        ff->abort_requested = 1;
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
