/**
 * @file ff.c
 * @brief Engine top-level: lifecycle, source evaluator, and the inner
 *        interpreter.
 *
 * Two implementations of @ref ff_exec are compiled here, selected at
 * build time by the toolchain:
 *
 *  - **GCC / Clang** (`#ifndef _MSC_VER`): computed-goto threaded
 *    dispatch.  Each handler ends with `goto *dt[*ip++]` — one
 *    indirect branch, no switch overhead, no bounds compare.
 *
 *  - **MSVC** (`#ifdef _MSC_VER`): classic `switch (*ip++)` loop.
 *    MSVC optimises this to an indirect jump table comparable to the
 *    GCC path.
 *
 * Both versions share the same prologue (`ff_exec_setup_p.h`), the
 * same built-in word bodies (`words/ff_words_*_p.h`), and the same
 * exit-label / macro-cleanup epilogue (`ff_exec_teardown_p.h`).  The
 * only platform-specific code in each function is the dispatch-table
 * initialisation (GCC) or the `for (;;) switch` wrapper (MSVC), plus
 * the `_FF_NEXT()` and `_FF_CASE(op)` macro definitions.
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

/* ====================================================================
 * GCC / Clang implementation — computed-goto threaded dispatch.
 *
 * Each handler ends with `_FF_NEXT()` which expands to a direct
 * indirect branch into the static dispatch table: one `jmp *(%reg)`
 * per opcode, no switch overhead, no bounds compare on the hot path.
 * ==================================================================== */
#ifndef _MSC_VER

/** @copydoc ff_exec */
bool ff_exec(ff_t *ff, ff_word_t *w)
{
#include "ff_exec_setup_p.h"

    /* Dispatch table (static, zero-initialised on first load).
       Populated lazily at _ff_fill_dt, placed AFTER all lbl_XX labels
       so &&label expressions reference already-defined labels — this
       satisfies Clang's requirement that label addresses not be forward
       references.  All writes are idempotent, so a benign race on the
       very first concurrent call is safe. */
    static void *dt[FF_OP_COUNT];

    #define _FF_CASE(op)  lbl_##op:
    #define _FF_NEXT()    goto *dt[(ff_opcode_t)*ip++]

    /* First call: jump past the handlers to fill the table. */
    if (ff_unlikely(!dt[FF_OP_CALL]))
        goto _ff_fill_dt;
    _FF_NEXT();

    /* Structural escape hatch: external C word. The external fn
       accesses the data stack through ff->stack only, so sync/restore
       the cached TOS around the call. */
    _FF_CASE(FF_OP_CALL)
        {
            ff_word_fn fn = (ff_word_fn)(intptr_t)*ip++;
            _FF_SYNC();
            fn(ff);
            _FF_RESTORE();
        }
        if (!ip)
            goto done;
        _FF_NEXT();

    /* Built-in word bodies live in per-category headers included here
       so each handler is inline.  The headers reference the macros
       (_FF_NEXT, _FF_CASE, _FF_SYNC, _FF_RESTORE, _FF_SO, _FF_RSO),
       labels (done, broken), and local variables (S, R, BT, ip, ff,
       bt_size) in this scope. */
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

    lbl_unknown:
        /* Trusted builds keep the unreachable hint so the compiler can
           elide bounds checks.  Safe builds raise a clean error instead
           of UB on heap corruption / stale ip. */
#if FF_SAFE_MEM
        _FF_SYNC();
        ff_tracef(ff, FF_SEV_ERROR | FF_ERR_BAD_OPCODE,
                  "Bad opcode 0x%llx.",
                  (unsigned long long)*(ip - 1));
        goto broken;
#else
        FF_UNREACHABLE();
#endif

    /* --- Dispatch table fill (first call only) ---
       All lbl_XX labels are defined above this point, so &&label is
       a backward reference and both GCC and Clang accept it. */
_ff_fill_dt:
    for (int _i = 0; _i < FF_OP_COUNT; _i++)
        dt[_i] = &&lbl_unknown;
    dt[FF_OP_CALL]             = &&lbl_FF_OP_CALL;
    dt[FF_OP_NEST]             = &&lbl_FF_OP_NEST;
    dt[FF_OP_TNEST]            = &&lbl_FF_OP_TNEST;
    dt[FF_OP_EXIT]             = &&lbl_FF_OP_EXIT;
    dt[FF_OP_LIT]              = &&lbl_FF_OP_LIT;
    dt[FF_OP_LIT0]             = &&lbl_FF_OP_LIT0;
    dt[FF_OP_LIT1]             = &&lbl_FF_OP_LIT1;
    dt[FF_OP_LITM1]            = &&lbl_FF_OP_LITM1;
    dt[FF_OP_LITADD]           = &&lbl_FF_OP_LITADD;
    dt[FF_OP_LITSUB]           = &&lbl_FF_OP_LITSUB;
    dt[FF_OP_FLIT]             = &&lbl_FF_OP_FLIT;
    dt[FF_OP_STRLIT]           = &&lbl_FF_OP_STRLIT;
    dt[FF_OP_BRANCH]           = &&lbl_FF_OP_BRANCH;
    dt[FF_OP_QBRANCH]          = &&lbl_FF_OP_QBRANCH;
    dt[FF_OP_DOES_RUNTIME]     = &&lbl_FF_OP_DOES_RUNTIME;
    dt[FF_OP_CREATE_RUNTIME]   = &&lbl_FF_OP_CREATE_RUNTIME;
    dt[FF_OP_CONSTANT_RUNTIME] = &&lbl_FF_OP_CONSTANT_RUNTIME;
    dt[FF_OP_ARRAY_RUNTIME]    = &&lbl_FF_OP_ARRAY_RUNTIME;
    dt[FF_OP_DEFER_RUNTIME]    = &&lbl_FF_OP_DEFER_RUNTIME;
    dt[FF_OP_VAR_FETCH]        = &&lbl_FF_OP_VAR_FETCH;
    dt[FF_OP_VAR_STORE]        = &&lbl_FF_OP_VAR_STORE;
    dt[FF_OP_VAR_PLUS_STORE]   = &&lbl_FF_OP_VAR_PLUS_STORE;
    dt[FF_OP_DUP]              = &&lbl_FF_OP_DUP;
    dt[FF_OP_DROP]             = &&lbl_FF_OP_DROP;
    dt[FF_OP_SWAP]             = &&lbl_FF_OP_SWAP;
    dt[FF_OP_OVER]             = &&lbl_FF_OP_OVER;
    dt[FF_OP_ROT]              = &&lbl_FF_OP_ROT;
    dt[FF_OP_NROT]             = &&lbl_FF_OP_NROT;
    dt[FF_OP_PICK]             = &&lbl_FF_OP_PICK;
    dt[FF_OP_ROLL]             = &&lbl_FF_OP_ROLL;
    dt[FF_OP_DEPTH]            = &&lbl_FF_OP_DEPTH;
    dt[FF_OP_CLEAR]            = &&lbl_FF_OP_CLEAR;
    dt[FF_OP_TO_R]             = &&lbl_FF_OP_TO_R;
    dt[FF_OP_FROM_R]           = &&lbl_FF_OP_FROM_R;
    dt[FF_OP_FETCH_R]          = &&lbl_FF_OP_FETCH_R;
    dt[FF_OP_2DUP]             = &&lbl_FF_OP_2DUP;
    dt[FF_OP_2DROP]            = &&lbl_FF_OP_2DROP;
    dt[FF_OP_2SWAP]            = &&lbl_FF_OP_2SWAP;
    dt[FF_OP_2OVER]            = &&lbl_FF_OP_2OVER;
    dt[FF_OP_ADD]              = &&lbl_FF_OP_ADD;
    dt[FF_OP_SUB]              = &&lbl_FF_OP_SUB;
    dt[FF_OP_MUL]              = &&lbl_FF_OP_MUL;
    dt[FF_OP_DIV]              = &&lbl_FF_OP_DIV;
    dt[FF_OP_MOD]              = &&lbl_FF_OP_MOD;
    dt[FF_OP_DIVMOD]           = &&lbl_FF_OP_DIVMOD;
    dt[FF_OP_MIN]              = &&lbl_FF_OP_MIN;
    dt[FF_OP_MAX]              = &&lbl_FF_OP_MAX;
    dt[FF_OP_NEGATE]           = &&lbl_FF_OP_NEGATE;
    dt[FF_OP_ABS]              = &&lbl_FF_OP_ABS;
    dt[FF_OP_AND]              = &&lbl_FF_OP_AND;
    dt[FF_OP_OR]               = &&lbl_FF_OP_OR;
    dt[FF_OP_XOR]              = &&lbl_FF_OP_XOR;
    dt[FF_OP_NOT]              = &&lbl_FF_OP_NOT;
    dt[FF_OP_SHIFT]            = &&lbl_FF_OP_SHIFT;
    dt[FF_OP_EQ]               = &&lbl_FF_OP_EQ;
    dt[FF_OP_NEQ]              = &&lbl_FF_OP_NEQ;
    dt[FF_OP_LT]               = &&lbl_FF_OP_LT;
    dt[FF_OP_GT]               = &&lbl_FF_OP_GT;
    dt[FF_OP_LE]               = &&lbl_FF_OP_LE;
    dt[FF_OP_GE]               = &&lbl_FF_OP_GE;
    dt[FF_OP_ZERO_EQ]          = &&lbl_FF_OP_ZERO_EQ;
    dt[FF_OP_ZERO_NEQ]         = &&lbl_FF_OP_ZERO_NEQ;
    dt[FF_OP_ZERO_LT]          = &&lbl_FF_OP_ZERO_LT;
    dt[FF_OP_ZERO_GT]          = &&lbl_FF_OP_ZERO_GT;
    dt[FF_OP_INC]              = &&lbl_FF_OP_INC;
    dt[FF_OP_DEC]              = &&lbl_FF_OP_DEC;
    dt[FF_OP_INC2]             = &&lbl_FF_OP_INC2;
    dt[FF_OP_DEC2]             = &&lbl_FF_OP_DEC2;
    dt[FF_OP_MUL2]             = &&lbl_FF_OP_MUL2;
    dt[FF_OP_DIV2]             = &&lbl_FF_OP_DIV2;
    dt[FF_OP_SET_BASE]         = &&lbl_FF_OP_SET_BASE;
    dt[FF_OP_FADD]             = &&lbl_FF_OP_FADD;
    dt[FF_OP_FSUB]             = &&lbl_FF_OP_FSUB;
    dt[FF_OP_FMUL]             = &&lbl_FF_OP_FMUL;
    dt[FF_OP_FDIV]             = &&lbl_FF_OP_FDIV;
    dt[FF_OP_FNEGATE]          = &&lbl_FF_OP_FNEGATE;
    dt[FF_OP_FABS]             = &&lbl_FF_OP_FABS;
    dt[FF_OP_FSQRT]            = &&lbl_FF_OP_FSQRT;
    dt[FF_OP_FSIN]             = &&lbl_FF_OP_FSIN;
    dt[FF_OP_FCOS]             = &&lbl_FF_OP_FCOS;
    dt[FF_OP_FTAN]             = &&lbl_FF_OP_FTAN;
    dt[FF_OP_FASIN]            = &&lbl_FF_OP_FASIN;
    dt[FF_OP_FACOS]            = &&lbl_FF_OP_FACOS;
    dt[FF_OP_FATAN]            = &&lbl_FF_OP_FATAN;
    dt[FF_OP_FATAN2]           = &&lbl_FF_OP_FATAN2;
    dt[FF_OP_FEXP]             = &&lbl_FF_OP_FEXP;
    dt[FF_OP_FLOG]             = &&lbl_FF_OP_FLOG;
    dt[FF_OP_FPOW]             = &&lbl_FF_OP_FPOW;
    dt[FF_OP_F_DOT]            = &&lbl_FF_OP_F_DOT;
    dt[FF_OP_FLOAT]            = &&lbl_FF_OP_FLOAT;
    dt[FF_OP_FIX]              = &&lbl_FF_OP_FIX;
    dt[FF_OP_PI]               = &&lbl_FF_OP_PI;
    dt[FF_OP_E_CONST]          = &&lbl_FF_OP_E_CONST;
    dt[FF_OP_FEQ]              = &&lbl_FF_OP_FEQ;
    dt[FF_OP_FNEQ]             = &&lbl_FF_OP_FNEQ;
    dt[FF_OP_FLT]              = &&lbl_FF_OP_FLT;
    dt[FF_OP_FGT]              = &&lbl_FF_OP_FGT;
    dt[FF_OP_FLE]              = &&lbl_FF_OP_FLE;
    dt[FF_OP_FGE]              = &&lbl_FF_OP_FGE;
    dt[FF_OP_DOT]              = &&lbl_FF_OP_DOT;
    dt[FF_OP_QUESTION]         = &&lbl_FF_OP_QUESTION;
    dt[FF_OP_CR]               = &&lbl_FF_OP_CR;
    dt[FF_OP_EMIT]             = &&lbl_FF_OP_EMIT;
    dt[FF_OP_TYPE]             = &&lbl_FF_OP_TYPE;
    dt[FF_OP_DOT_S]            = &&lbl_FF_OP_DOT_S;
    dt[FF_OP_DOT_PAREN]        = &&lbl_FF_OP_DOT_PAREN;
    dt[FF_OP_DOTQUOTE]         = &&lbl_FF_OP_DOTQUOTE;
    dt[FF_OP_XDO]              = &&lbl_FF_OP_XDO;
    dt[FF_OP_XQDO]             = &&lbl_FF_OP_XQDO;
    dt[FF_OP_XLOOP]            = &&lbl_FF_OP_XLOOP;
    dt[FF_OP_PXLOOP]           = &&lbl_FF_OP_PXLOOP;
    dt[FF_OP_LOOP_I]           = &&lbl_FF_OP_LOOP_I;
    dt[FF_OP_LOOP_J]           = &&lbl_FF_OP_LOOP_J;
    dt[FF_OP_LEAVE]            = &&lbl_FF_OP_LEAVE;
    dt[FF_OP_I_ADD]            = &&lbl_FF_OP_I_ADD;
    dt[FF_OP_I_ADD_LOOP]       = &&lbl_FF_OP_I_ADD_LOOP;
    dt[FF_OP_NIP]              = &&lbl_FF_OP_NIP;
    dt[FF_OP_TUCK]             = &&lbl_FF_OP_TUCK;
    dt[FF_OP_OVER_PLUS]        = &&lbl_FF_OP_OVER_PLUS;
    dt[FF_OP_R_PLUS]           = &&lbl_FF_OP_R_PLUS;
    dt[FF_OP_DUP_ADD]          = &&lbl_FF_OP_DUP_ADD;
    dt[FF_OP_COLON]            = &&lbl_FF_OP_COLON;
    dt[FF_OP_SEMICOLON]        = &&lbl_FF_OP_SEMICOLON;
    dt[FF_OP_IMMEDIATE]        = &&lbl_FF_OP_IMMEDIATE;
    dt[FF_OP_LBRACKET]         = &&lbl_FF_OP_LBRACKET;
    dt[FF_OP_RBRACKET]         = &&lbl_FF_OP_RBRACKET;
    dt[FF_OP_TICK]             = &&lbl_FF_OP_TICK;
    dt[FF_OP_BRACKET_TICK]     = &&lbl_FF_OP_BRACKET_TICK;
    dt[FF_OP_EXECUTE]          = &&lbl_FF_OP_EXECUTE;
    dt[FF_OP_STATE]            = &&lbl_FF_OP_STATE;
    dt[FF_OP_BRACKET_COMPILE]  = &&lbl_FF_OP_BRACKET_COMPILE;
    dt[FF_OP_LITERAL]          = &&lbl_FF_OP_LITERAL;
    dt[FF_OP_COMPILE]          = &&lbl_FF_OP_COMPILE;
    dt[FF_OP_DOES]             = &&lbl_FF_OP_DOES;
    dt[FF_OP_QDUP]             = &&lbl_FF_OP_QDUP;
    dt[FF_OP_IF]               = &&lbl_FF_OP_IF;
    dt[FF_OP_ELSE]             = &&lbl_FF_OP_ELSE;
    dt[FF_OP_THEN]             = &&lbl_FF_OP_THEN;
    dt[FF_OP_BEGIN]            = &&lbl_FF_OP_BEGIN;
    dt[FF_OP_UNTIL]            = &&lbl_FF_OP_UNTIL;
    dt[FF_OP_AGAIN]            = &&lbl_FF_OP_AGAIN;
    dt[FF_OP_WHILE]            = &&lbl_FF_OP_WHILE;
    dt[FF_OP_REPEAT]           = &&lbl_FF_OP_REPEAT;
    dt[FF_OP_DO]               = &&lbl_FF_OP_DO;
    dt[FF_OP_QDO]              = &&lbl_FF_OP_QDO;
    dt[FF_OP_LOOP]             = &&lbl_FF_OP_LOOP;
    dt[FF_OP_PLOOP]            = &&lbl_FF_OP_PLOOP;
    dt[FF_OP_QUIT]             = &&lbl_FF_OP_QUIT;
    dt[FF_OP_ABORT]            = &&lbl_FF_OP_ABORT;
    dt[FF_OP_THROW]            = &&lbl_FF_OP_THROW;
    dt[FF_OP_CATCH]            = &&lbl_FF_OP_CATCH;
    dt[FF_OP_ABORTQ]           = &&lbl_FF_OP_ABORTQ;
    dt[FF_OP_CREATE]           = &&lbl_FF_OP_CREATE;
    dt[FF_OP_FORGET]           = &&lbl_FF_OP_FORGET;
    dt[FF_OP_VARIABLE]         = &&lbl_FF_OP_VARIABLE;
    dt[FF_OP_CONSTANT]         = &&lbl_FF_OP_CONSTANT;
    dt[FF_OP_DEFER]            = &&lbl_FF_OP_DEFER;
    dt[FF_OP_IS]               = &&lbl_FF_OP_IS;
    dt[FF_OP_HERE]             = &&lbl_FF_OP_HERE;
    dt[FF_OP_STORE]            = &&lbl_FF_OP_STORE;
    dt[FF_OP_FETCH]            = &&lbl_FF_OP_FETCH;
    dt[FF_OP_PLUS_STORE]       = &&lbl_FF_OP_PLUS_STORE;
    dt[FF_OP_ALLOT]            = &&lbl_FF_OP_ALLOT;
    dt[FF_OP_COMMA]            = &&lbl_FF_OP_COMMA;
    dt[FF_OP_C_STORE]          = &&lbl_FF_OP_C_STORE;
    dt[FF_OP_C_FETCH]          = &&lbl_FF_OP_C_FETCH;
    dt[FF_OP_C_COMMA]          = &&lbl_FF_OP_C_COMMA;
    dt[FF_OP_C_ALIGN]          = &&lbl_FF_OP_C_ALIGN;
    dt[FF_OP_STRING]           = &&lbl_FF_OP_STRING;
    dt[FF_OP_S_STORE]          = &&lbl_FF_OP_S_STORE;
    dt[FF_OP_S_CAT]            = &&lbl_FF_OP_S_CAT;
    dt[FF_OP_STRLEN]           = &&lbl_FF_OP_STRLEN;
    dt[FF_OP_STRCMP]           = &&lbl_FF_OP_STRCMP;
    dt[FF_OP_EVALUATE]         = &&lbl_FF_OP_EVALUATE;
    dt[FF_OP_LOAD]             = &&lbl_FF_OP_LOAD;
    dt[FF_OP_FIND]             = &&lbl_FF_OP_FIND;
    dt[FF_OP_TO_NAME]          = &&lbl_FF_OP_TO_NAME;
    dt[FF_OP_TO_BODY]          = &&lbl_FF_OP_TO_BODY;
    dt[FF_OP_ARRAY]            = &&lbl_FF_OP_ARRAY;
    dt[FF_OP_SYSTEM]           = &&lbl_FF_OP_SYSTEM;
    dt[FF_OP_STDIN]            = &&lbl_FF_OP_STDIN;
    dt[FF_OP_STDOUT]           = &&lbl_FF_OP_STDOUT;
    dt[FF_OP_STDERR]           = &&lbl_FF_OP_STDERR;
    dt[FF_OP_FOPEN]            = &&lbl_FF_OP_FOPEN;
    dt[FF_OP_FCLOSE]           = &&lbl_FF_OP_FCLOSE;
    dt[FF_OP_FGETS]            = &&lbl_FF_OP_FGETS;
    dt[FF_OP_FPUTS]            = &&lbl_FF_OP_FPUTS;
    dt[FF_OP_FGETC]            = &&lbl_FF_OP_FGETC;
    dt[FF_OP_FPUTC]            = &&lbl_FF_OP_FPUTC;
    dt[FF_OP_FTELL]            = &&lbl_FF_OP_FTELL;
    dt[FF_OP_FSEEK]            = &&lbl_FF_OP_FSEEK;
    dt[FF_OP_SEEK_SET]         = &&lbl_FF_OP_SEEK_SET;
    dt[FF_OP_SEEK_CUR]         = &&lbl_FF_OP_SEEK_CUR;
    dt[FF_OP_SEEK_END]         = &&lbl_FF_OP_SEEK_END;
    dt[FF_OP_ERRNO]            = &&lbl_FF_OP_ERRNO;
    dt[FF_OP_STRERROR]         = &&lbl_FF_OP_STRERROR;
    dt[FF_OP_TRACE]            = &&lbl_FF_OP_TRACE;
    dt[FF_OP_BACKTRACE]        = &&lbl_FF_OP_BACKTRACE;
    dt[FF_OP_DUMP]             = &&lbl_FF_OP_DUMP;
    dt[FF_OP_MEMSTAT]          = &&lbl_FF_OP_MEMSTAT;
    dt[FF_OP_WORDS]            = &&lbl_FF_OP_WORDS;
    dt[FF_OP_WORDSUSED]        = &&lbl_FF_OP_WORDSUSED;
    dt[FF_OP_WORDSUNUSED]      = &&lbl_FF_OP_WORDSUNUSED;
    dt[FF_OP_MAN]              = &&lbl_FF_OP_MAN;
    dt[FF_OP_DUMP_WORD]        = &&lbl_FF_OP_DUMP_WORD;
    dt[FF_OP_SEE]              = &&lbl_FF_OP_SEE;
    _FF_NEXT();   /* redo the first dispatch with the table now full */

#include "ff_exec_teardown_p.h"
}

/* ====================================================================
 * MSVC implementation — switch-based dispatch.
 * ==================================================================== */
#else  /* _MSC_VER */

/** @copydoc ff_exec */
bool ff_exec(ff_t *ff, ff_word_t *w)
{
#include "ff_exec_setup_p.h"

    #define _FF_CASE(op)  case op:
    #define _FF_NEXT()    break

    for (;;)
    {
        switch (*ip++)
        {
            /* Structural escape hatch: external C word. */
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

#include "ff_exec_teardown_p.h"
}

#endif  /* _MSC_VER */

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
